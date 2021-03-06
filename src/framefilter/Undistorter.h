//******************************************************************************
//* File:   Undistorter.h
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
//******************************************************************************

#ifndef OAT_UNDISTORTER_H
#define	OAT_UNDISTORTER_H

#include "FrameFilter.h"

namespace oat {

/**
 * Lens distortion compensation.
 *
 * Uses the results of oat-calibrate.
 */
class Undistorter : public FrameFilter {
public:

    /**
     * Lens distortion compensation.
     *
     * Uses the results of oat-calibrate. To reverse radial and tangential distortion
     * introduced by the camera lens and array mounting imperfections.
     * @param frame_source_address raw frame source address
     * @param frame_sink_address filtered frame sink address
     */
    Undistorter(const std::string &frame_souce_address,
                const std::string &frame_sink_address);

    void configure(const std::string &config_file,
                   const std::string &config_key) override;

    // Accessors
    void set_camera_matrix(const cv::Matx33d& value) { camera_matrix_ = value; }
    void set_distortion_coefficients(const cv::Mat& value) { distortion_coefficients_ = value.clone(); }

private:

    // Camera model to use for calibration
    // TODO: Declared/defined in oat-calibrate as well. Common header?
    enum class CameraModel
    {
        NA      = -1,  //!< Not applicable
        PINHOLE =  0,  //!< Pinhole camera model
        FISHEYE =  1   //!< Fisheye lens model
    };

    /**
     * Apply undistortion filter.
     * @param frame Unfiltered frame
     * @return Filtered frame
     */
    void filter(cv::Mat& frame) override;

    CameraModel camera_model_ {CameraModel::PINHOLE};
    cv::Matx33d camera_matrix_  {cv::Matx33d::eye()};
    std::vector<double> distortion_coefficients_ {0,0,0,0,0,0,0,0};

    // Negative implied no rotation
    double rotation_deg_ = 0.0;
    cv::Matx23d rotation_matrix_; 
};

}      /* namespace oat */
#endif /* OAT_UNDISTORTER_H */

