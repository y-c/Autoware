#include <ros/ros.h>
#include <opencv/cv.h>
#include <opencv/highgui.h>

#if (CV_MAJOR_VERSION == 3)
#include <opencv2/imgcodecs/imgcodecs.hpp>
#endif

#include <QString>
#include <QImage>

#include "image_viewer_plugin.h"
#include "draw_rects.h"
#include "draw_points.h"

#define XSTR(x) #x
#define STR(x) XSTR(x)

namespace integrated_viewer {
    const QString     ImageViewerPlugin::kImageDataType                 = "sensor_msgs/Image";
    const QString     ImageViewerPlugin::kDetectedObjectDataTypeBase    = "autoware_msgs/DetectedObjectArray";
    const QString     ImageViewerPlugin::kPointDataType                 = "autoware_msgs/PointsImage";
    const QString     ImageViewerPlugin::kLaneDataType                  = "autoware_msgs/ImageLaneObjects";
    const QString     ImageViewerPlugin::kBlankTopic                    = "-----";

    ImageViewerPlugin::ImageViewerPlugin(QWidget *parent)
            : rviz::Panel(parent) {

        // Initialize Form
        ui_.setupUi(this);

        // Set point size parameter
        ui_.point_size_spin_box_->setMinimum(2); // minimum point size is 2x2
        ui_.point_size_spin_box_->setValue(3);   // Set default size to 3

        // Load default image
        default_image_ = cv::imread(STR(IMAGE_VIEWER_DEFAULT_IMAGE));

        points_msg_ = NULL;
        detected_objects_msg_ = NULL;
        lane_msg_ = NULL;

        UpdateTopicList();

        viewed_image_ = default_image_.clone();
        default_image_shown_ = true;
        ShowImageOnUi();

        // If combobox is clicked, topic list will be update
        ui_.image_topic_combo_box_->installEventFilter(this);
        ui_.rect_topic_combo_box_->installEventFilter(this);
        ui_.point_topic_combo_box_->installEventFilter(this);
        ui_.lane_topic_combo_box_->installEventFilter(this);

    } // ImageViewerPlugin::ImageViewerPlugin()


    void ImageViewerPlugin::UpdateTopicList(void) {
        // The topic list that can be selected from the UI
        QStringList image_topic_list;
        QStringList rect_topic_list;
        QStringList point_topic_list;
        QStringList lane_topic_list;

        // The topic name currently chosen
        QString image_topic_current = ui_.image_topic_combo_box_->currentText();
        QString rect_topic_current = ui_.rect_topic_combo_box_->currentText();
        QString point_topic_current = ui_.point_topic_combo_box_->currentText();
        QString lane_topic_current = ui_.lane_topic_combo_box_->currentText();

        if (image_topic_current == "") {
            image_topic_current = kBlankTopic;
        }

        if (rect_topic_current == "") {
            rect_topic_current = kBlankTopic;
        }

        if (point_topic_current == "") {
            point_topic_current = kBlankTopic;
        }

        if (lane_topic_current == "") {
            lane_topic_current = kBlankTopic;
        }

        // Insert blank topic name to the top of the lists
        image_topic_list << kBlankTopic;
        rect_topic_list << kBlankTopic;
        point_topic_list << kBlankTopic;
        lane_topic_list << kBlankTopic;

        // Get all available topic
        ros::master::V_TopicInfo master_topics;
        ros::master::getTopics(master_topics);

        // Analyse topics
        for (ros::master::V_TopicInfo::iterator it = master_topics.begin(); it != master_topics.end(); it++) {
            const ros::master::TopicInfo &info = *it;
            const QString topic_name = QString::fromStdString(info.name);
            const QString topic_type = QString::fromStdString(info.datatype);

            // Check whether this topic is image
            if (topic_type.contains(kImageDataType) == true) {
                image_topic_list << topic_name;
                continue;
            }

            // Check whether this topic is rectangle
            if (topic_type.contains(kDetectedObjectDataTypeBase) == true) {
                rect_topic_list << topic_name;
                continue;
            }

            // Check whether this topic is point cloud
            if (topic_type.contains(kPointDataType) == true) {
                point_topic_list << topic_name;
                continue;
            }

            // Check whether this topic is lane
            if (topic_type.contains(kLaneDataType) == true) {
                lane_topic_list << topic_name;
                continue;
            }
        }

        // remove all list items from combo box
        ui_.image_topic_combo_box_->clear();
        ui_.rect_topic_combo_box_->clear();
        ui_.point_topic_combo_box_->clear();
        ui_.lane_topic_combo_box_->clear();

        // set new items to combo box
        ui_.image_topic_combo_box_->addItems(image_topic_list);
        ui_.rect_topic_combo_box_->addItems(rect_topic_list);
        ui_.point_topic_combo_box_->addItems(point_topic_list);
        ui_.lane_topic_combo_box_->addItems(lane_topic_list);

        ui_.image_topic_combo_box_->insertSeparator(1);
        ui_.rect_topic_combo_box_->insertSeparator(1);
        ui_.point_topic_combo_box_->insertSeparator(1);
        ui_.lane_topic_combo_box_->insertSeparator(1);

        // set last topic as current
        int image_topic_index = ui_.image_topic_combo_box_->findText(image_topic_current);
        int rect_topic_index = ui_.rect_topic_combo_box_->findText(rect_topic_current);
        int point_topic_index = ui_.point_topic_combo_box_->findText(point_topic_current);
        int lane_topic_index = ui_.lane_topic_combo_box_->findText(lane_topic_current);

        if (image_topic_index != -1) {
            ui_.image_topic_combo_box_->setCurrentIndex(image_topic_index);
        }

        if (rect_topic_index != -1) {
            ui_.rect_topic_combo_box_->setCurrentIndex(rect_topic_index);
        }

        if (point_topic_index != -1) {
            ui_.point_topic_combo_box_->setCurrentIndex(point_topic_index);
        }

        if (lane_topic_index != -1) {
            ui_.lane_topic_combo_box_->setCurrentIndex(lane_topic_index);
        }

    } // ImageViewerPlugin::UpdateTopicList()





    // The behavior of combo box for image
    void ImageViewerPlugin::on_image_topic_combo_box__activated(int index) {
        // Extract selected topic name from combo box
        std::string selected_topic = ui_.image_topic_combo_box_->itemText(index).toStdString();
        if (selected_topic == kBlankTopic.toStdString() || selected_topic == "") {
            image_sub_.shutdown();
            // If blank name is selected as image topic, show default image
            viewed_image_ = default_image_.clone();
            default_image_shown_ = true;
            ShowImageOnUi();
            return;
        }

        // if selected topic is not blank or empty, start callback function
        default_image_shown_ = false;
        image_sub_ = node_handle_.subscribe<sensor_msgs::Image>(selected_topic,
                                                                1,
                                                                &ImageViewerPlugin::ImageCallback,
                                                                this);

    } // ImageViewerPlugin::on_image_topic_combo_box__activated()


    void ImageViewerPlugin::ImageCallback(const sensor_msgs::Image::ConstPtr &msg) {
        // Get image from topic
        const auto &encoding = sensor_msgs::image_encodings::BGR8;
        viewed_image_ = cv_bridge::toCvCopy(msg, encoding)->image;

        ShowImageOnUi();
    } // ImageViewerPlugin::ImageCallback()


    // The behavior of combo box for detection result rectangle
    void ImageViewerPlugin::on_rect_topic_combo_box__activated(int index) {
        // Extract selected topic name from combo box
        std::string selected_topic = ui_.rect_topic_combo_box_->itemText(index).toStdString();
        if (selected_topic == kBlankTopic.toStdString() || selected_topic == "") {
            rect_sub_.shutdown();
            detected_objects_msg_ = NULL;
            return;
        }

        // Switch booted callback function by topic name
        detected_objects_msg_ = NULL;
        // this topic type is image_obj
        rect_sub_ = node_handle_.subscribe<autoware_msgs::DetectedObjectArray>(selected_topic,
                                                                     1,
                                                                     &ImageViewerPlugin::DetectedObjCallback,
                                                                     this);
    } // ImageViewerPlugin::on_detectedobj_topic_combo_box__activated()


    void ImageViewerPlugin::DetectedObjCallback(const autoware_msgs::DetectedObjectArray::ConstPtr &msg) {
        detected_objects_msg_ = msg;
    } // ImageViewerPlugin::DetectedObjCallback()

    // The behavior of combo box for points image
    void ImageViewerPlugin::on_point_topic_combo_box__activated(int index) {
        // Extract selected topic name from combo box
        std::string selected_topic = ui_.point_topic_combo_box_->itemText(index).toStdString();
        if (selected_topic == kBlankTopic.toStdString() || selected_topic == "") {
            point_sub_.shutdown();
            points_msg_ = NULL;
            return;
        }

        // if selected topic is not blank or empty , start callback function
        point_sub_ = node_handle_.subscribe<autoware_msgs::PointsImage>(selected_topic,
                                                                        1,
                                                                        &ImageViewerPlugin::PointCallback,
                                                                        this);

    } // ImageViewerPlugin::on_point_topic_combo_box__activated()


    void ImageViewerPlugin::PointCallback(const autoware_msgs::PointsImage::ConstPtr &msg) {
        points_msg_ = msg;
    } // ImageViewerPlugin::PointCallback()


    // The behavior of combo box for detected lane
    void ImageViewerPlugin::on_lane_topic_combo_box__activated(int index) {
        // Extract selected topic name from combo box
        std::string selected_topic = ui_.lane_topic_combo_box_->itemText(index).toStdString();
        if (selected_topic == kBlankTopic.toStdString() || selected_topic == "") {
            lane_sub_.shutdown();
            lane_msg_ = NULL;
            return;
        }

        // if selected topic is not blank or emtpy, start callback function
        lane_sub_ = node_handle_.subscribe<autoware_msgs::ImageLaneObjects>(selected_topic,
                                                                            1,
                                                                            &ImageViewerPlugin::LaneCallback,
                                                                            this);
    }  // void ImageViewerPlugin::on_lane_topic_combo_box__activated()


    void ImageViewerPlugin::LaneCallback(const autoware_msgs::ImageLaneObjects::ConstPtr &msg) {
        lane_msg_ = msg;
    }

    void ImageViewerPlugin::ShowImageOnUi(void) {
        // Additional things will be drawn if shown image is not default one
        if (!default_image_shown_) {
            // Draw detection result rectangles on the image
            rects_drawer_.DrawImageObj(detected_objects_msg_, viewed_image_);

            // Draw points on the image
            int point_size = ui_.point_size_spin_box_->value();
            points_drawer_.Draw(points_msg_, viewed_image_, point_size);

            // Draw lane on the image
            lane_drawer_.Draw(lane_msg_, viewed_image_);
        }
        // Convert cv::Mat to QPixmap to show modified image on the UI
        QPixmap view_on_ui = convert_image::CvMatToQPixmap(viewed_image_);

        // Reflect image on UI
        int height = ui_.view_->height();
        int width = ui_.view_->width();
        ui_.view_->setPixmap(view_on_ui.scaled(width,
                                               height,
                                               Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation));
    } // ImageViewerPlugin::ShowImageOnUi()


    void ImageViewerPlugin::resizeEvent(QResizeEvent *) {
        ShowImageOnUi();
    } // ImageViewerPlugin::resizeEvent()


    bool ImageViewerPlugin::eventFilter(QObject *object, QEvent *event) {
        if (event->type() == QEvent::MouseButtonPress) {
            // combo box will update its contents if this filter is applied
            UpdateTopicList();
        }

        return QObject::eventFilter(object, event);
    }


} // end namespace integrated_viewer


// Tell pluginlib about this class.  Every class which should be
// loadable by pluginlib::ClassLoader must have these two lines
// compiled in its .cpp file, outside of any namespace scope.
#include <pluginlib/class_list_macros.h>

PLUGINLIB_EXPORT_CLASS(integrated_viewer::ImageViewerPlugin, rviz::Panel)
// END_TUTORIAL
