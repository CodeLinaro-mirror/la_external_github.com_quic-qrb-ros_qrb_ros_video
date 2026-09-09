/*
**************************************************************************************************
* Copyright (c) Qualcomm Technologies, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
**************************************************************************************************
*/

#ifndef QRB_ROS_VIDEO_TEST_BASE_NODE_HPP_
#define QRB_ROS_VIDEO_TEST_BASE_NODE_HPP_

#include <gst/gst.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rclcpp/rclcpp.hpp"

namespace qrb_ros
{
namespace video
{

/**
 * @brief Base node template class for GStreamer-based ROS nodes
 */
class BaseNode : public rclcpp::Node
{
public:
  explicit BaseNode(const std::string & node_name, const rclcpp::NodeOptions & options)
    : Node(node_name, options)
    , format_(this->declare_parameter("format", "h264"))
    , width_(this->declare_parameter("width", "640"))
    , height_(this->declare_parameter("height", "480"))
    , framerate_(this->declare_parameter("framerate", "30/1"))
    , log_level_(this->declare_parameter("log-level", "warning"))
    , url_(this->declare_parameter("url", ""))
  {
    // Initialize GStreamer
    if (!gst_is_initialized()) {
      gst_init(nullptr, nullptr);
    }

    auto pos = framerate_.find_first_of('/');
    std::string denominator = "1";
    if (pos != std::string::npos) {
      denominator = framerate_.substr(pos + 1);
    }
    std::string numerator = framerate_.substr(0, pos);
    if (std::stof(denominator) != 0) {
      fps_ = std::stof(numerator) / std::stof(denominator);
    }

    // Set ROS Node log level
    auto log_level = rclcpp::Logger::Level::Warn;
    if (log_level_ == "info") {
      log_level = rclcpp::Logger::Level::Info;
    } else if (log_level_ == "debug") {
      log_level = rclcpp::Logger::Level::Debug;
    }

    get_logger().set_level(log_level);
  }

  virtual ~BaseNode() = default;

protected:
  /**
   * @brief Setup the GStreamer pipeline
   */
  virtual void setup_pipeline() = 0;

  // Common logging utility
  void log_pipeline_creation(const std::string & pipeline_str)
  {
    RCLCPP_WARN(this->get_logger(), "Creating pipeline: %s", pipeline_str.c_str());
  }

  // Shared state for the parsebin-based discovery pass
  struct ParseDiscoverData
  {
    BaseNode * self = nullptr;
    GMainLoop * loop = nullptr;
    guint timeout_id = 0;
    bool found = false;
    bool timed_out = false;
    std::string codec;
    std::string width;
    std::string height;
    std::string framerate;
    float fps = 0.0f;
  };

  // Called when parsebin exposes a parsed elementary stream. We only read the
  // caps of the first video pad; no decoder is ever instantiated.
  static void on_parsebin_pad_added(GstElement *, GstPad * pad, gpointer user_data)
  {
    auto * data = static_cast<ParseDiscoverData *>(user_data);
    if (data->found) {
      return;
    }

    GstCaps * caps = gst_pad_get_current_caps(pad);
    if (!caps) {
      caps = gst_pad_query_caps(pad, nullptr);
    }
    if (!caps) {
      return;
    }

    const GstStructure * structure = gst_caps_get_structure(caps, 0);
    const gchar * name = structure ? gst_structure_get_name(structure) : nullptr;
    if (!name || !g_str_has_prefix(name, "video/")) {
      gst_caps_unref(caps);
      return;
    }

    gchar * caps_str = gst_caps_to_string(caps);
    RCLCPP_INFO(data->self->get_logger(), "Video caps: %s", caps_str);
    g_free(caps_str);

    if (g_str_has_suffix(name, "h264")) {
      data->codec = "h264";
    } else if (g_str_has_suffix(name, "h265") || g_str_has_suffix(name, "hevc")) {
      data->codec = "h265";
    } else {
      data->codec = name;
    }

    gint width = 0;
    gint height = 0;
    if (gst_structure_get_int(structure, "width", &width) && width > 0) {
      data->width = std::to_string(width);
    }
    if (gst_structure_get_int(structure, "height", &height) && height > 0) {
      data->height = std::to_string(height);
    }

    gint fps_num = 0;
    gint fps_denom = 0;
    if (gst_structure_get_fraction(structure, "framerate", &fps_num, &fps_denom) &&
        fps_num > 0 && fps_denom > 0) {
      data->framerate = std::to_string(fps_num) + "/" + std::to_string(fps_denom);
      data->fps = static_cast<float>(fps_num) / static_cast<float>(fps_denom);
    }

    data->found = true;
    gst_caps_unref(caps);

    // We have what we need - stop the discovery loop.
    if (data->loop && g_main_loop_is_running(data->loop)) {
      g_main_loop_quit(data->loop);
    }
  }

  static gboolean bus_watch_cb(GstBus *, GstMessage * msg, gpointer user_data)
  {
    auto * data = static_cast<ParseDiscoverData *>(user_data);
    switch (GST_MESSAGE_TYPE(msg)) {
      case GST_MESSAGE_ERROR:
      case GST_MESSAGE_EOS:
        if (data->loop && g_main_loop_is_running(data->loop)) {
          g_main_loop_quit(data->loop);
        }
        break;
      default:
        break;
    }
    return TRUE;
  }

  static gboolean discover_timeout_cb(gpointer user_data)
  {
    auto * data = static_cast<ParseDiscoverData *>(user_data);
    data->timed_out = true;
    data->timeout_id = 0;
    if (data->loop && g_main_loop_is_running(data->loop)) {
      g_main_loop_quit(data->loop);
    }
    return G_SOURCE_REMOVE;
  }

  bool discover_pipeline()
  {
    if (url_.empty()) {
      RCLCPP_ERROR(this->get_logger(), "No URL provided for file discovery");
      return false;
    }

    // Discover the pipeline based on the format
    if (format_ == "mp4") {
      RCLCPP_INFO(this->get_logger(), "Discovering video codec from MP4 file: %s", url_.c_str());

      // Use a "filesrc ! parsebin" pipeline instead of GstDiscoverer/uridecodebin.
      // parsebin only autoplugs demuxers and parsers - it deliberately stops before
      // decoders, so no video hardware codec plugin is loaded during discovery.
      // The parser's source-pad caps carry codec, resolution and framerate.
      std::string location = url_;
      const std::string file_prefix = "file://";
      if (location.rfind(file_prefix, 0) == 0) {
        location = location.substr(file_prefix.size());
      }

      GstElement * pipeline = gst_pipeline_new("discover-pipeline");
      GstElement * src = gst_element_factory_make("filesrc", nullptr);
      GstElement * parse = gst_element_factory_make("parsebin", nullptr);
      if (!pipeline || !src || !parse) {
        RCLCPP_ERROR(this->get_logger(), "Failed to create parsebin discovery pipeline");
        if (src) {
          gst_object_unref(src);
        }
        if (parse) {
          gst_object_unref(parse);
        }
        if (pipeline) {
          gst_object_unref(pipeline);
        }
        return false;
      }

      g_object_set(src, "location", location.c_str(), nullptr);
      gst_bin_add_many(GST_BIN(pipeline), src, parse, nullptr);
      if (!gst_element_link(src, parse)) {
        RCLCPP_ERROR(this->get_logger(), "Failed to link filesrc to parsebin");
        gst_object_unref(pipeline);
        return false;
      }

      ParseDiscoverData data;
      data.self = this;
      data.loop = g_main_loop_new(nullptr, FALSE);

      g_signal_connect(parse, "pad-added", G_CALLBACK(on_parsebin_pad_added), &data);

      GstBus * bus = gst_element_get_bus(pipeline);
      guint bus_watch_id = gst_bus_add_watch(bus, bus_watch_cb, &data);
      data.timeout_id = g_timeout_add_seconds(2, discover_timeout_cb, &data);

      gst_element_set_state(pipeline, GST_STATE_PAUSED);
      g_main_loop_run(data.loop);

      if (data.timeout_id != 0) {
        g_source_remove(data.timeout_id);
      }
      g_source_remove(bus_watch_id);
      gst_element_set_state(pipeline, GST_STATE_NULL);
      gst_object_unref(bus);
      g_main_loop_unref(data.loop);
      gst_object_unref(pipeline);

      if (!data.found) {
        if (data.timed_out) {
          RCLCPP_ERROR(this->get_logger(), "Timed out discovering video stream in the file");
        } else {
          RCLCPP_ERROR(this->get_logger(), "No valid video stream found in the file");
        }
        return false;
      }

      pixel_format_ = data.codec;
      RCLCPP_INFO(this->get_logger(), "Detected codec: %s", pixel_format_.c_str());

      if (!data.width.empty()) {
        width_ = data.width;
      }
      if (!data.height.empty()) {
        height_ = data.height;
      }
      if (!data.framerate.empty()) {
        framerate_ = data.framerate;
        if (data.fps > 0.0f && data.fps != fps_) {
          RCLCPP_WARN(this->get_logger(), "Detected framerate: %s (%f fps)",
              data.framerate.c_str(), data.fps);
          fps_ = data.fps;
        }
      }

      RCLCPP_INFO(this->get_logger(), "Video properties: %sx%s @ %s fps", width_.c_str(),
          height_.c_str(), framerate_.c_str());

      RCLCPP_INFO(this->get_logger(), "Successfully discovered video information");
      return true;
    }

    // For non-MP4 formats, we assume the format is correctly specified
    RCLCPP_INFO(this->get_logger(), "Using specified format: %s", format_.c_str());
    return true;
  }

  // Common error handling for pipeline creation
  bool create_pipeline(const std::string & pipeline_str)
  {
    GError * error = nullptr;

    log_pipeline_creation(pipeline_str);

    pipeline_ = gst_parse_launch(pipeline_str.c_str(), &error);

    if (error) {
      RCLCPP_ERROR(this->get_logger(), "Failed to create pipeline: %s", error->message);
      g_error_free(error);
      return false;
    }

    return true;
  }

  std::string format_;
  std::string pixel_format_;
  std::string width_;
  std::string height_;
  std::string framerate_;
  std::string log_level_;
  std::string url_;
  float fps_ = 30.0f;
  GstElement * pipeline_ = nullptr;
};

}  // namespace video
}  // namespace qrb_ros

#endif  // QRB_ROS_VIDEO_TEST_BASE_NODE_HPP_