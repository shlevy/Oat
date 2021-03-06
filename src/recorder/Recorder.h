//******************************************************************************
//* File:   Recorder.h
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
//*****************************************************************************

#ifndef OAT_RECORDER_H
#define OAT_RECORDER_H

#include <atomic>
#include <condition_variable>
#include <string>
#include <thread>
#include <boost/dynamic_bitset.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <rapidjson/filewritestream.h>
#include <rapidjson/prettywriter.h>

#include "../../lib/shmemdf/Helpers.h"
#include "../../lib/shmemdf/Source.h"
#include "../../lib/shmemdf/Sink.h"
#include "../../lib/datatypes/Frame.h"
#include "../../lib/datatypes/Position2D.h"

namespace oat {
namespace blf = boost::lockfree;

// Constants
static constexpr int FRAME_WRITE_BUFFER_SIZE {1000};
static constexpr int POSITION_WRITE_BUFFER_SIZE {65536};

// Forward decl.
class SharedFrameHeader;

/**
 * Position and frame recorder.
 */
class Recorder {

    // The controlRecorder routine needs access to Recorder's private members
    friend int controlRecorder(std::istream &in,
                               oat::Recorder &recorder,
                               bool print_cmd);

public:

    //using PositionSource = std::pair < std::string, std::unique_ptr
    //                                 < oat::Source
    //                                 < oat::Position2D > > >;

    //using FrameSource = std::pair < std::string, std::unique_ptr
    //                              < oat::Source<oat::SharedFrameHeader > > >;

    using FrameQueue = 
        blf::spsc_queue<
            oat::Frame, boost::lockfree::capacity<FRAME_WRITE_BUFFER_SIZE>
        >;

    using pvec_size_t =
        std::vector<oat::NamedSource<oat::Position2D>>::size_type; 
    using fvec_size_t =
        std::vector<oat::NamedSource<oat::SharedFrameHeader>>::size_type;

    //using pvec_size_t = std::vector<oat::Position2D>::size_type;
    //using fvec_size_t = std::vector<FrameSource>::size_type;

    /**
     * Position and frame recorder.
     * @param position_source_addresses Addresses specifying position SOURCES
     * to record
     * @param frame_source_addresses Addresses specifying frame SOURCES to
     * record
     */
    Recorder(const std::vector<std::string> &position_source_addresses,
             const std::vector<std::string> &frame_source_addresses);

    ~Recorder();

    /** 
     * @brief Create and initialize recording file(s). Must be called
     * before writeStreams.
     * 
     */
    void initializeRecording(void);

    /**
     * Recorder SOURCEs must be able to connect to a NODEs from
     * which to receive positions and frames.
     */
    void connectToNodes(void);

    /** Collect frames and positions from SOURCES. Write frames and positions
     * to file.  
     * @return SOURCE end-of-stream signal. If true, this component should
     * exit.
     */
    bool writeStreams(void);

    /**
     * Get recorder name
     * @return name
     */
    std::string name(void) { return name_; }

    // Accessors
    bool record_on(void) const { return record_on_; }
    void set_record_on(const bool value) { record_on_ = value; }
    bool source_eof(void) const { return source_eof_; }
    bool recording_initialized(void) const { return recording_initialized_; }
    void set_save_path(const std::string &value) { save_path_ = value; }
    void set_file_name(const std::string &value) { file_name_ = value; }
    void set_prepend_timestamp(const bool value) { prepend_timestamp_ = value; }
    void set_prepend_source(const bool value) { prepend_source_ = value; }
    void set_allow_overwrite(const bool value) { allow_overwrite_ = value; } 
    void set_verbose_file(const bool value) {verbose_file_ = value; };

private:

    // Name of this recorder
    std::string name_;

    // Recorder in running state (i.e. all threads should remain responsive
    // for new data coming down the pipeline)
    std::atomic<bool> running_ {true};

    // Recording gate can be toggled on and off interactively from other
    // threads and processes
    std::atomic<bool> record_on_ {true};

    // Sample rate of this recorder
    // The true sample rate is enforced by the slowest SOURCE since all SOURCEs
    // are sychronized. User will be warned if SOURCE sample rates differ.
    double sample_rate_hz_ {0.0};

    // Folder in which files will be saved
    std::string save_path_ {"."};

    // Base file name
    std::string file_name_ {""};

    // Determines if should file_name be prepended with a timestamp
    bool prepend_timestamp_ {false};

    // Determines if the (first) SOURCE name should be appended to the file name
    bool prepend_source_ {false};

    // Determines if existing file will be overwritten
    bool allow_overwrite_ {false};

    // Determines if indeterminate position data fields should be written in
    // spite of being indeterminate for sample parsing ease? e.g. Should we
    // write pos_xy when pos_ok = false?
    bool verbose_file_ {true};

    // Determines if the recording machinary is ready to use.
    bool recording_initialized_ {false};

    // Source end of file flag
    bool source_eof_ {false};

    // Video files
    std::vector< std::string > video_file_names_;
    std::vector< std::unique_ptr
               < cv::VideoWriter > > video_writers_;

    // Position file
    FILE * position_fp_ {nullptr};
    char position_write_buffer[POSITION_WRITE_BUFFER_SIZE];
    std::unique_ptr<rapidjson::FileWriteStream> file_stream_;
    rapidjson::PrettyWriter<rapidjson::FileWriteStream> json_writer_ {*file_stream_};

    // Frame sources
    oat::NamedSourceList<oat::SharedFrameHeader> frame_sources_;
    boost::dynamic_bitset<> frame_read_required_;

    // Multi video writer multi-threading
    std::vector< std::unique_ptr
               < std::thread > > frame_write_threads_;
    std::vector< std::unique_ptr
               < std::mutex > > frame_write_mutexes_;
    std::vector< std::unique_ptr
               < std::condition_variable > > frame_write_condition_variables_;
    std::vector< std::unique_ptr
               < FrameQueue > > frame_write_buffers_;

    // Position sources
    std::vector<oat::Position2D> positions_;
    std::vector<uint64_t> position_write_number_;
    oat::NamedSourceList<oat::Position2D> position_sources_;
    std::string position_file_name_;

    void initializeVideoWriter(cv::VideoWriter& writer,
                               const std::string &file_name,
                               const oat::Frame &image);

    void writeFramesToFileFromBuffer(uint32_t writer_idx);
    void writePositionsToFile(void);
    void writePositionFileHeader(const std::string& date,
                                 const double sample_rate,
                                 const std::vector<std::string>& sources);

};

}      /* namespace oat */
#endif /* OAT_RECORDER_H */
