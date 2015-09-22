//******************************************************************************
//* File:   CameraCalibrator.cpp
//* Author: Jon Newman <jpnewman snail mit dot edu>
//*
//* Copyright (c) Jon Newman (jpnewman snail mit dot edu)
//* All right reserved.
//* This file is part of the Oat project.
//* This is free software: you can redistribute it and/or modify
//* it under the terms of the GNU General Public License as published by
//* the Free Software Foundation, either version 3 of the License, or
//* (at your option) any later version.
//* This software is distributed in the hope that it will be useful,
//* but WITHOUT ANY WARRANTY; without even the implied warranty of
//* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//* GNU General Public License for more details.
//* You should have received a copy of the GNU General Public License
//* along with this source code.  If not, see <http://www.gnu.org/licenses/>.
//****************************************************************************

#include "OatConfig.h" // Generated by CMake

#include <utility> 

#include <boost/io/ios_state.hpp>
#include <opencv2/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/core/utility.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include "../../lib/cpptoml/cpptoml.h"
#include "../../lib/cpptoml/OatTOMLSanitize.h"
#include "../../lib/utility/IOFormat.h"

#include "Saver.h"
#include "UsagePrinter.h"
#include "PathChanger.h"
#include "CameraCalibrator.h"


CameraCalibrator::CameraCalibrator(
        const std::string& frame_source_name,
        const CameraModel& model,
        cv::Size& chessboard_size,
        double square_size_meters) :
  Calibrator(frame_source_name)
, chessboard_size_(chessboard_size)
, square_size_meters_(square_size_meters)
, model_(model)
{

    // if (interactive_) { // TODO: Generalize to accept images specified by a file without interactive session

    // Initialize corner detection update timers
    tick_ = Clock::now();
    tock_ = Clock::now();

    // Generate true corner locations based upon the chessboard size and
    // square size
    for (int i = 0; i < chessboard_size_.height; i++) {
        for (int j = 0; j < chessboard_size_.width; j++) {
            corners_meters_.push_back(
                    cv::Point3f(static_cast<double>(j) * square_size_meters_,
                                static_cast<double>(i) * square_size_meters_,
                                0.0f));
        }
    }

#ifdef OAT_USE_OPENGL
        try {
            cv::namedWindow(name(), cv::WINDOW_OPENGL & cv::WINDOW_KEEPRATIO);
        } catch (cv::Exception& ex) {
            oat::whoWarn(name(), "OpenCV not compiled with OpenGL support."
                    "Falling back to OpenCV's display driver.\n");
            cv::namedWindow(name(), cv::WINDOW_NORMAL & cv::WINDOW_KEEPRATIO);
        }
#else
        cv::namedWindow(name(), cv::WINDOW_NORMAL & cv::WINDOW_KEEPRATIO);
#endif

        std::cout << "Starting interactive session.\n";
   // }
}

void CameraCalibrator::configure(const std::string& config_file, const std::string& config_key) {

    // TODO: Provide list of image paths to perform calibraiton directly from file.

    // Available options
    std::vector<std::string> options {""};

    // This will throw cpptoml::parse_exception if a file
    // with invalid TOML is provided
    cpptoml::table config;
    config = cpptoml::parse_file(config_file);

    // See if a camera configuration was provided
    if (config.contains(config_key)) {

        // Get this components configuration table
        auto this_config = config.get_table(config_key);

        // Check for unknown options in the table and throw if you find them
        oat::config::checkKeys(options, this_config);

    } else {
        throw (std::runtime_error(oat::configNoTableError(config_key, config_file)));
    }
}

void CameraCalibrator::calibrate(cv::Mat& frame) {

    tick_ = Clock::now();

    // TODO: Is it possible to get frame metadata before any loops? Might
    // be hard if client (this) strarts first since the frame source is not
    // yet known and therefore things like frame size are not known.
    frame_size_ = frame.size();

    if (mode_ == Mode::DETECT) {
        detectChessboard(frame);
    }

    // Add mode and status info
    decorateFrame(frame);

    cv::imshow(name(), frame);
    char command = cv::waitKey(1);

    switch (command) {

        case 'd': // Enter/exit chessboard corner capture mode
        {
            if (requireMode(std::forward<Mode>(Mode::NORMAL), std::forward<Mode>(Mode::DETECT))) 
                toggleDetectMode();
            break;
        }
        case 'f': // Change the calibration save path
        {
            if (requireMode(std::forward<Mode>(Mode::NORMAL))) {
                PathChanger changer;
                accept(&changer); 
            }
            break;
        }
        case 'g': // Generate calibration parameters
        {
            if (requireMode(std::forward<Mode>(Mode::NORMAL)))
                generateCalibrationParameters();
            break;
        }
        case 'h': // Display help dialog
        {
            //UsagePrinter usage;
            //accept(&usage, std::cout);
            break;
        }
        case 'm': // Select homography estimation method
        {
            //selectCalibrationMethod();
            break;
        }
        case 'p': // Print calibration results
        {
            printCalibrationResults(std::cout);
            break;
        }
        case 'u': // Undistort mode
        {
            if (requireMode(std::forward<Mode>(Mode::NORMAL), std::forward<Mode>(Mode::UNDISTORT)))
                toggleUndistortMode();
            break;
        }
        case 's': // Save homography info
        {
            Saver saver("calibration", calibration_save_path_);
            accept(&saver);
            break;
        }
    }
}

void CameraCalibrator::accept(CalibratorVisitor* visitor) {

    visitor->visit(this);
}

void CameraCalibrator::accept(OutputVisitor* visitor, std::ostream& out) {

    visitor->visit(this, out);
}

void CameraCalibrator::detectChessboard(cv::Mat& frame) {

    // Extract the chessboard from the current image
    std::vector<cv::Point2f> point_buffer;
    bool detected =
        cv::findChessboardCorners(frame, chessboard_size_, point_buffer,
        cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_FAST_CHECK | cv::CALIB_CB_NORMALIZE_IMAGE);

    // Draw corners on the frame
    cv::drawChessboardCorners(frame, chessboard_size_, cv::Mat(point_buffer), detected);

    // Calculate elapsed time since last detection
    if (detected) {

        Milliseconds elapsed_time =
            std::chrono::duration_cast<Milliseconds>(tick_ - tock_);

        if (elapsed_time > min_detection_delay_) {

            std::cout << "Chessboard detected.\n";

            // Reset timer
            tock_ = Clock::now();

            // Subpixel corner location estimation termination criteria
            // Max iterations = 30;
            // Desired accuracy of pixel resolution = 0.1
            cv::TermCriteria term(
                    cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.1);

            // Generate grey-scale image
            cv::Mat frame_grey;
            cv::cvtColor(frame, frame_grey, cv::COLOR_BGR2GRAY);

            // Find exact corner locations
            cv::cornerSubPix(frame_grey, point_buffer, cv::Size(11, 11),
                    cv::Size(-1, -1), term);

            // Push the new corners into storage
            corners_.push_back(point_buffer);

            // Note visually that we have added new corners to our data set
            cv::bitwise_not(frame, frame);
        }
    }
}

void CameraCalibrator::generateCalibrationParameters() {

    // TODO: user options for the following
    // Fix the aspect ratio of the lens (ratio of lens focal lengths for each
    // dimension of its internal reference frame, fc(2)/fc(1) where fc =
    // [KK[1,1]; KK[2,2])
    // calibration_flags_ += CALIB_FIX_ASPECT_RATIO;

    // Set tangential distortion coefficients (last three elements of KC) to
    // zero. This is reasonable for modern cameras that have very good
    // centering of lens over the sensory array.
    // calibration_flags_ += CALIB_ZERO_TANGENT_DIST

    // Make principle point cc = [KK[3,1]; KK[3,2]] equal to the center of the
    // frame : cc = [(nx-1)/2;(ny-1)/2)]
    // calibration_flags_ += CALIB_FIX_PRINCIPAL_POINT

    camera_matrix_ = cv::Mat::eye(3, 3, CV_64F);
    distortion_coefficients_ = cv::Mat::zeros(8, 1, CV_64F);

    std::vector<std::vector<cv::Point3f>> object_points; // { corners_meters_ };
    object_points.resize(corners_.size(), corners_meters_);

    //if (flags & CALIB_FIX_ASPECT_RATIO)
    //    cameraMatrix.at<double>(0, 0) = aspectRatio;

    // Unused currently
    std::vector<cv::Mat> rotation, translation;

    rms_error_ = cv::calibrateCamera(object_points,
                                     corners_,
                                     frame_size_,
                                     camera_matrix_,
                                     distortion_coefficients_,
                                     rotation,
                                     translation,
                                     calibration_flags_ | cv::CALIB_FIX_K4 | cv::CALIB_FIX_K5);

    // TODO: Is it? Do we need some minimum error for this to be true?
    calibration_valid_ = true;

}

void CameraCalibrator::decorateFrame(cv::Mat& frame) {
      
    // Calculate text positions
   // mode_position;
   // data_size_position;

    switch (mode_) {

        case Mode::NORMAL :
        {
            // Just put mode indicator
            break;
        }
        case Mode::DETECT :
        {
            // Print number of captured images
            break;
        }
        case Mode::UNDISTORT :
        {
            // Print number of captured images
            break;
        }

    }
//        if (mode_ == Mode::DETECT) {
//            if (undistortImage)
//                msg = format("%d/%d Undist", (int) imagePoints.size(), nframes);
//            else
//                msg = format("%d/%d", (int) imagePoints.size(), nframes);
//        }
//
//        cv::putText(frame, msg, textOrigin, 1, 1,
//                calibration_valid_ ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 0));
}
void CameraCalibrator::printCalibrationResults(std::ostream& out) {

    // Save stream state. When ifs is destructed, the stream will
    // return to default format.
    boost::io::ios_flags_saver ifs(out);

    out << "Camera Matrix:\n"
        << camera_matrix_ << "\n\n"
        << "Distortion Coefficients:\n"
        << distortion_coefficients_ << "\n\n"
        << "RMS Reconstruction Error:\n"
        << rms_error_ << "\n\n";
}

// TODO: Move mode indicators to image
void CameraCalibrator::toggleDetectMode() {

    if (mode_ != Mode::DETECT) {
        std::cout << "Capture mode on.\n";
        mode_ = Mode::DETECT;
    } else {
        std::cout << "Capture mode off.\n";
        mode_ = Mode::NORMAL;
    }
}

// TODO: Move mode indicators to image
void CameraCalibrator::toggleUndistortMode() {

    if (mode_ != Mode::UNDISTORT) {
        std::cout << "Undistort mode on.\n";
        mode_ = Mode::UNDISTORT;
    } else {
        std::cout << "Undistort mode off.\n";
        mode_ = Mode::NORMAL;
    }
}
