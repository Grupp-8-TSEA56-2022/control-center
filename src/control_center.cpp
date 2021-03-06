#include "control_center.h"
#include "map_node.h"
#include "log.h"

#include <list>
#include <vector>
#include <string>
#include <sstream>

using namespace std;

ControlCenter::ControlCenter(size_t obstacle_distance_filter_len,
                             size_t stop_distance_filter_len,
                             int consecutive_param,
                             int high_count_param,
                             unsigned status_code_threshold)
: obstacle_distance_filter{obstacle_distance_filter_len, 100},
  stop_distance_filter{stop_distance_filter_len, 0},
  stop_line_detector{consecutive_param, high_count_param},
  status_code_threshold{status_code_threshold} {
    Logger::log(INFO, __FILE__, "ControlCenter", "Initialize ControlCenter");
}

void ControlCenter::update_map(json m) {
    path_finder.update_map(m);
}

void ControlCenter::add_drive_instruction(drive_instruction_t drive_instruction) {
    drive_instructions.push_back(drive_instruction);
}

void ControlCenter::add_drive_instruction(instruction::InstructionNumber instruction, string id) {
    drive_instruction_t drive_instruction{};
    drive_instruction.number = instruction;
    drive_instruction.id = id;
    drive_instructions.push_back(drive_instruction);
}

control_t ControlCenter::operator()(
        int obstacle_distance, int stop_distance, int speed,
        int angle_left, int angle_right, int lateral_left, int lateral_right,
        int image_processing_status_code) {
    stringstream ss;
    ss << "obstacle_distance=" << obstacle_distance
       << ", stop_distance=" << stop_distance
       << ", speed=" << speed
       << ", angles=" << angle_left << "," << angle_right
       << ", status_code=" << image_processing_status_code;
    Logger::log(DEBUG, __FILE__, "start", ss.str());
    control_t control_data = {0, 0, 0, regulation_mode::auto_nominal};

    if (stop_distance == -1)
        stop_distance = 1000;

    if (obstacle_distance == 0)
        obstacle_distance = 1000;

    obstacle_distance = obstacle_distance_filter(obstacle_distance);
    stop_distance = stop_distance_filter(stop_distance);
    ss.str("");
    ss << "obstacle_distance=" << obstacle_distance
       << " stop_distance=" << stop_distance;
    Logger::log(DEBUG, __FILE__, "Filtered values", ss.str());

    update_state(obstacle_distance, stop_distance, speed);

    choose_regulation_mode(&control_data, image_processing_status_code);
    control_data.angle = calculate_angle(angle_left, angle_right);
    control_data.lateral_position = calculate_lateral_position(lateral_left, lateral_right);
    control_data.speed_ref = calculate_speed();

    ss.str("");
    ss << "state=" << state
       << ", angle=" << control_data.angle
       << ", lateral=" << control_data.lateral_position
       << ", speed_ref=" << control_data.speed_ref
       << ", drive mode=" << control_data.regulation_mode;
    Logger::log(DEBUG, __FILE__, "done", ss.str());

    return control_data;
}

void ControlCenter::update_state(int obstacle_distance, int stop_distance, int speed) {
    drive_instruction_t intr{};

    if (drive_instructions.empty()) {
        // No instruction
        if (state != state::stop_line) {
            Logger::log(ERROR, __FILE__, "Update state", "No instruction but state not stop_line");
        }
        if (speed > 0) {
            state = state::stopping;
            stop_reason = state::stop_line;
        } else {
            state = state::stop_line;
        }
        return;
    } else {
        intr = drive_instructions.front();
    }

    switch (state) {
        case state::normal:
        case state::intersection:
            if (path_blocked(obstacle_distance)) {
                Logger::log(INFO, __FILE__, "Update state", "Path blocked, stopping");
                state = state::stopping;
                stop_reason = state::blocked;
            } else if (stop_line_detector.at_line(stop_distance)) {
                // At node
                if (drive_instructions.size() > 1) {
                    finish_instruction();
                    set_new_state(speed);
                } else {
                    Logger::log(INFO, __FILE__, "Update state", "At stop line, stopping");
                    finish_when_stopped = true;
                    state = state::stopping;
                    stop_reason = state::stop_line;
                }
            } else {
                // Clear path, don't change state
                Logger::log(DEBUG, __FILE__, "Update state", "Running");
            }
            break;

        case state::stop_line:
            if (path_blocked(obstacle_distance)) {
                state = state::blocked;
                Logger::log(INFO, __FILE__, "Update state", "Path blocked");
                break;
            }
            if (drive_instructions.front().number == instruction::stop) {
                finish_instruction();
            }
            if (stop_line_detector.at_line(stop_distance)) {
                Logger::log(ERROR, __FILE__, "Update state", "Still at stop line");
            }
            set_new_state(speed);
            break;

        case state::blocked:
            if (!path_blocked(obstacle_distance)) {
                // The path is no longer blocked
                Logger::log(INFO, __FILE__, "Update state", "Path no longer blocked");
                set_new_state(speed);
            }
            break;

        case state::stopping:
            if (speed == 0) {
                Logger::log(INFO, __FILE__, "Update state", "Stopped");
                state = stop_reason;
                if (finish_when_stopped) {
                    finish_instruction();
                    finish_when_stopped = false;
                }
            }
            break;

        default:
            Logger::log(ERROR, __FILE__, "Update state", "Unknown state");
    }
}

void ControlCenter::set_new_state(int speed) {
    enum instruction::InstructionNumber instr{};
    enum state::ControlState new_state{};
    string state_name{};
    if (drive_instructions.empty()) {
        // No instruction
        instr = instruction::stop;
    } else {
        instr = drive_instructions.front().number;
    }
    switch (instr) {
        case instruction::forward:
            new_state = state::normal;
            state_name = "normal";
            break;
        case instruction::left:
        case instruction::right:
            new_state = state::intersection;
            state_name = "intersection";
            break;
        case instruction::stop:
            if (speed > 0) {
                stop_reason = state::stop_line;
                new_state = state::stopping;
                state_name = "stopping";
            } else {
                new_state = state::stop_line;
            }
            break;
        default:
            Logger::log(ERROR, __FILE__, "Set new state", "Unknown instruction");
            new_state = state::stop_line;
            state_name = "stop_line";
            break;
    }
    if (new_state != state) {
        Logger::log(INFO, __FILE__, "Set new state", state_name);
        state = new_state;
    }
}

void ControlCenter::finish_instruction() {
    Logger::log(INFO, __FILE__, "ControlCenter", "Finishing instruction");
    string id = drive_instructions.front().id;
    drive_instructions.pop_front();
    if (!road_segments.empty())
        road_segments.pop_front();
    finished_id_buffer.push_back(id);
}

string ControlCenter::get_current_road_segment() {
    if (road_segments.empty()) {
        return "end";
    } else {
        return road_segments.front();
    }
}

drive_instruction_t ControlCenter::get_current_drive_instruction() {
    return drive_instructions.front();
}

void ControlCenter::set_drive_missions(list<string> target_list) {
    string start_node = target_list.front();
    target_list.pop_front();

    // Reset position
    drive_instructions.clear();
    road_segments.clear();

    for (string target_node : target_list) {
        // Stop instruction between missions
        add_drive_instruction(instruction::stop, start_node);
        road_segments.push_back(start_node);

        // Solve
        path_finder.solve(start_node, target_node);
        vector<instruction::InstructionNumber> new_instructions = path_finder.get_drive_mission();
        list<string> new_segments = path_finder.get_road_segments();

        // Save path
        auto inst_itr = new_instructions.begin();
        auto segm_itr = new_segments.begin();
        while (inst_itr != new_instructions.end()) {
            add_drive_instruction(*inst_itr, *segm_itr);
            ++inst_itr;
            ++segm_itr;
        }
        road_segments.splice(road_segments.end(), new_segments);

        start_node = target_node;
    }
}

int ControlCenter::calculate_speed() const {
    switch (state) {
        case state::normal:
            return DEFAULT_SPEED;

        case state::intersection:
            return INTERSECTION_SPEED;

        case state::stop_line:
        case state::stopping:
        case state::blocked:
            return 0;

        default:
            Logger::log(ERROR, __FILE__, "ControlCenter", "Unknown state while calculating speed");
            return 0;
    }
}

void ControlCenter::choose_regulation_mode(control_t *control_data, int status_code) {
    // If status code is 0 and it has been for a while, use regulation mode
    // nominal. Otherwise critical.
    if (status_code == 0) {
        ++consecutive_0_status_codes;
    } else {
        consecutive_0_status_codes = 0;
    }
    if (consecutive_0_status_codes >= status_code_threshold) {
        control_data->regulation_mode = regulation_mode::auto_nominal;
    } else {
        control_data->regulation_mode = regulation_mode::auto_critical;
    }
}

int ControlCenter::calculate_lateral_position(int lateral_left, int lateral_right) const {
    instruction::InstructionNumber instr{drive_instructions.front().number};
    switch (instr) {
        case instruction::forward:
            return (lateral_left + lateral_right) / 2;
        case instruction::left:
            return lateral_left;
        case instruction::right:
            return lateral_right;
        default:
            Logger::log(ERROR, __FILE__, "choose_angle_and_lageral", "Unknown state");
            return 0;
    }
}

int ControlCenter::calculate_angle(int angle_left, int angle_right) {
    /* Calculates what angle to use. The basic idea is to use an average if the
     * car should go straight but use the one to left/right if the car should
     * follow that line (in an intersection). However sometimes the data is
     * bad and the angle changes abruptly. Often only one angle is bad so we
     * then use the other one and hope to recover. */
    int angle{};
    instruction::InstructionNumber instr{drive_instructions.front().number};
    switch (instr) {
        case instruction::forward:
            if (is_expected(angle_left) && is_expected(angle_right)) {
                angle = (angle_left + angle_right) / 2;
            } else if (is_expected(angle_left)) {
                angle = angle_left;
            } else if (is_expected(angle_right)) {
                angle = angle_right;
            } else {
                // We could not recover
                angle = (angle_left + angle_right) / 2;
            }
            break;
        case instruction::left:
            if (is_expected(angle_left)) {
                angle = angle_left;
            } else if (is_expected(angle_right)) {
                angle = angle_right;
            } else {
                // We could not recover
                angle = angle_left;
            }
            break;
        case instruction::right:
            if (is_expected(angle_right)) {
                angle = angle_right;
            } else if (is_expected(angle_left)) {
                angle = angle_left;
            } else {
                // We could not recover
                angle = angle_right;
            }
            break;
        default:
            Logger::log(ERROR, __FILE__, "calculate_angle", "Unknown instruction");
            break;
    }
    last_angle = angle;
    return angle;
}

bool ControlCenter::finished_instruction() {
    return !finished_id_buffer.empty();
}

string ControlCenter::get_finished_instruction_id() {
    if (finished_id_buffer.empty()) {
        return "";
    } else {
        string id = finished_id_buffer.front();
        finished_id_buffer.pop_front();
        return id;
    }
}

enum state::ControlState ControlCenter::get_state() {
    return state;
}
