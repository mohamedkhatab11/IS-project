#include <bits/stdc++.h>

using namespace std;

string trim(const string& str) {
    size_t first = str.find_first_not_of(" \t");
    if (first == string::npos) return "";
    size_t last = str.find_last_not_of(" \t");
    return str.substr(first, last - first + 1);
}

string read_file(const string& filename) {
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "Error opening file: " << filename << endl;
        return "";
    }
    string content((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
    return content;
}

vector<vector<string>> parse_csv(const string& csv_content) {
    vector<vector<string>> data;
    stringstream ss(csv_content);
    string line;
    while (getline(ss, line)) {
        vector<string> row;
        stringstream ssline(line);
        string cell;
        bool in_quote = false;
        string temp = "";
        while (getline(ssline, cell, ',')) {
            if (in_quote) {
                temp += "," + cell;
                if (!cell.empty() && cell.back() == '"') {
                    temp = temp.substr(1, temp.size() - 2);
                    row.push_back(trim(temp));
                    in_quote = false;
                }
            }
            else {
                if (!cell.empty() && cell.front() == '"') {
                    if (cell.back() == '"') {
                        row.push_back(trim(cell.substr(1, cell.size() - 2)));
                    }
                    else {
                        in_quote = true;
                        temp = cell;
                    }
                }
                else {
                    row.push_back(trim(cell));
                }
            }
        }
        data.push_back(row);
    }
    return data;
}

struct TimeSlot {
    int id;
    string day;
    string startTime;
    string endTime;
};

struct Room {
    string id;
    string building;
    string space;
    int capacity;
    string type;
};

struct Instructor {
    int id;
    string name;
    string preferredSlots;
    vector<string> qualifiedCourses;
};

struct TA {
    int id;
    string name;
    string preferredSlots;
    map<string, string> qualifiedCourses; // course -> role
};

struct Section {
    string faculty;
    int year;
    string dept;
    int groupNumber;
    int sectionNumber;
    int studentNumber;
};

struct Course {
    int year;
    int semester;
    string specialization;
    string code;
    string title;
    int lecSlots;
    int tutSlots;
    int labSlots;
};

struct Session {
    string type; // "Lecture", "Tutorial", "Lab"
    string courseCode;
    int sectionIndex;
    int instance;
};

struct Assignment {
    int timeId = -1;
    int roomIndex = -1;
    int teacherIndex = -1; // index in instructors or tas depending on type
};

vector<TimeSlot> timeSlots;
vector<Room> rooms;
vector<Instructor> instructors;
vector<TA> tas;
vector<Section> sections;
vector<Course> courses;
vector<Session> sessions;
vector<Assignment> assignments;

void load_timeslots(const string& filename) {
    string content = read_file(filename);
    auto data = parse_csv(content);
    for (size_t i = 1; i < data.size(); ++i) {
        auto& row = data[i];
        if (row.size() < 4) continue;
        string day = row[0];
        string start = row[1];
        string end = row[2];
        int id = stoi(row[3]);
        timeSlots.push_back({ id, day, start, end });
    }
}

void load_rooms(const string& filename) {
    string content = read_file(filename);
    auto data = parse_csv(content);
    string curr_building = "";
    for (size_t i = 1; i < data.size(); ++i) {
        auto& row = data[i];
        if (row.size() < 4) continue;
        if (!row[0].empty()) curr_building = row[0];
        string space = row[1];
        if (space.empty()) continue;
        int cap = stoi(row[2]);
        string type = row[3];
        string id = curr_building + " " + space;
        rooms.push_back({ trim(id), trim(curr_building), trim(space), cap, trim(type) });
    }
}

void load_instructors(const string& filename) {
    string content = read_file(filename);
    auto data = parse_csv(content);
    for (size_t i = 1; i < data.size(); ++i) {
        auto& row = data[i];
        if (row.size() < 4) continue;
        int id = stoi(row[0]);
        string name = row[1];
        string pref = row[2];
        string qual_str = row[3];
        vector<string> quals;
        stringstream ss(qual_str);
        string course;
        while (getline(ss, course, ',')) {
            quals.push_back(trim(course));
        }
        instructors.push_back({ id, name, pref, quals });
    }
}

void load_tas(const string& filename) {
    string content = read_file(filename);
    auto data = parse_csv(content);
    for (size_t i = 1; i < data.size(); ++i) {
        auto& row = data[i];
        if (row.size() < 4) continue;
        int id = stoi(row[0]);
        string name = row[1];
        string pref = row[2];
        string qual_str = row[3];
        TA ta = { id, name, pref, {} };
        stringstream ss(qual_str);
        string token;
        while (getline(ss, token, ',')) {
            token = trim(token);
            size_t par_pos = token.find('(');
            if (par_pos != string::npos) {
                string course = trim(token.substr(0, par_pos));
                size_t end_par = token.rfind(')');
                string role = trim(token.substr(par_pos + 1, end_par - par_pos - 1));
                ta.qualifiedCourses[course] = role;
            }
        }
        tas.push_back(ta);
    }
}

void load_sections(const string& filename) {
    string content = read_file(filename);
    auto data = parse_csv(content);
    string curr_faculty = "", curr_dept = "";
    int curr_year = 0, curr_group = 0;
    for (size_t i = 1; i < data.size(); ++i) {
        auto& row = data[i];
        if (row.size() < 6) continue;
        if (!row[0].empty()) curr_faculty = row[0];
        if (!row[1].empty()) curr_year = stoi(row[1]);
        if (!row[2].empty()) curr_dept = row[2];
        if (!row[3].empty()) curr_group = stoi(row[3]);
        if (!row[4].empty() && !row[5].empty()) {
            int sec_num = stoi(row[4]);
            int stu_num = stoi(row[5]);
            sections.push_back({ curr_faculty, curr_year, curr_dept, curr_group, sec_num, stu_num });
        }
    }
}

void load_courses(const string& filename) {
    string content = read_file(filename);
    auto data = parse_csv(content);
    int curr_year_c = 0, curr_sem = 0;
    string curr_spec = "";
    for (size_t i = 1; i < data.size(); ++i) {
        auto& row = data[i];
        if (row.size() < 8) continue;
        if (!row[0].empty()) curr_year_c = stoi(row[0]);
        if (!row[1].empty()) curr_sem = stoi(row[1]);
        if (!row[2].empty()) curr_spec = row[2];
        string code = row[3];
        if (code.empty()) continue;
        string title = row[4];
        int lec = stoi(row[5]);
        int tut = stoi(row[6]);
        int lab = stoi(row[7]);
        courses.push_back({ curr_year_c, curr_sem, curr_spec, code, title, lec, tut, lab });
    }
}

bool match_room(const Session& s, const Room& room, const Section& sec) {
    if (room.capacity < sec.studentNumber) return false;
    string room_type = room.type;
    if (s.type == "Lecture" || s.type == "Tutorial") {
        if (room_type == "Classroom" || room_type == "Hall" || room_type == "Theater") return true;
    }
    else { // Lab
        if (s.courseCode.find("PHY") != string::npos) {
            if (room_type == "PHY_LAB") return true;
        }
        else if (s.courseCode.find("Drawing") != string::npos) {
            if (room_type == "Drawing Studio" || room_type == "FoE Drawing Lab") return true;
        }
        else if (room_type == "Computer Lab" || room_type == "Lab") return true;
    }
    return false;
}

bool check_constraints(int pos) {
    const Session& curr_session = sessions[pos];
    const Assignment& curr_assign = assignments[pos];
    int curr_time = curr_assign.timeId;
    int curr_room = curr_assign.roomIndex;
    int curr_teacher = curr_assign.teacherIndex;
    string curr_type = curr_session.type;
    int curr_sec = curr_session.sectionIndex;

    // Room conflict
    for (int i = 0; i < pos; ++i) {
        if (assignments[i].timeId == curr_time && assignments[i].roomIndex == curr_room) return false;
    }

    // Student group (section) conflict
    for (int i = 0; i < pos; ++i) {
        if (sessions[i].sectionIndex == curr_sec && assignments[i].timeId == curr_time) return false;
    }

    // Teacher conflict
    for (int i = 0; i < pos; ++i) {
        if (assignments[i].timeId == curr_time && assignments[i].teacherIndex == curr_teacher) {
            if (curr_type == "Lecture" && sessions[i].type == "Lecture") return false;
            if (curr_type != "Lecture" && sessions[i].type != "Lecture") return false;
        }
    }

    return true;
}

bool solve(int pos) {
    if (pos == sessions.size()) return true;

    const Session& s = sessions[pos];
    const Section& sec = sections[s.sectionIndex];

    vector<int> possible_times;
    for (int t = 0; t < timeSlots.size(); ++t) possible_times.push_back(t);

    vector<int> possible_rooms;
    for (int r = 0; r < rooms.size(); ++r) {
        if (match_room(s, rooms[r], sec)) possible_rooms.push_back(r);
    }

    vector<int> possible_teachers;
    if (s.type == "Lecture") {
        for (int i = 0; i < instructors.size(); ++i) {
            auto& quals = instructors[i].qualifiedCourses;
            if (find(quals.begin(), quals.end(), s.courseCode) != quals.end()) {
                possible_teachers.push_back(i);
            }
        }
    }
    else {
        for (int ta_idx = 0; ta_idx < tas.size(); ++ta_idx) {
            auto& qual_map = tas[ta_idx].qualifiedCourses;
            auto it = qual_map.find(s.courseCode);
            if (it != qual_map.end() &&
                ((s.type == "Tutorial" && it->second.find("TUT") != string::npos) ||
                    (s.type == "Lab" && it->second.find("LAB") != string::npos))) {
                possible_teachers.push_back(ta_idx);
            }
        }
    }

    for (int t : possible_times) {
        for (int r : possible_rooms) {
            for (int teach : possible_teachers) {
                assignments[pos] = { t, r, teach };
                if (check_constraints(pos)) {
                    if (solve(pos + 1)) return true;
                }
            }
        }
    }
    assignments[pos] = { -1, -1, -1 };
    return false;
}

void print_timetable() {
    for (int i = 0; i < sessions.size(); ++i) {
        const Session& s = sessions[i];
        const Assignment& a = assignments[i];
        const Section& sec = sections[s.sectionIndex];
        const TimeSlot& ts = timeSlots[a.timeId];
        const Room& rm = rooms[a.roomIndex];
        string teacher_name = (s.type == "Lecture") ? instructors[a.teacherIndex].name : tas[a.teacherIndex].name;

        cout << "Year: " << sec.year << ", Dept: " << sec.dept << ", Group: " << sec.groupNumber << ", Section: " << sec.sectionNumber << endl;
        cout << "Type: " << s.type << ", Course: " << s.courseCode << ", Instance: " << s.instance << endl;
        cout << "Time: " << ts.day << " " << ts.startTime << " - " << ts.endTime << endl;
        cout << "Room: " << rm.id << endl;
        cout << "Teacher: " << teacher_name << endl;
        cout << "------------------------" << endl;
    }
}

int main() {
    load_timeslots("TimeSlots.csv");
    load_rooms("Halls.csv");
    load_instructors("Instructor.csv");
    load_tas("TAs.csv");
    load_sections("Sections.csv");
    load_courses("Courses.csv");

    // Generate sessions for each section's courses
    for (const auto& c : courses) {
        if (c.lecSlots + c.tutSlots + c.labSlots == 0) continue;
        vector<int> relevant_sections;
        for (int si = 0; si < sections.size(); ++si) {
            const auto& sec = sections[si];
            if (sec.year == c.year && (c.specialization == "N/A" || sec.dept.empty() || sec.dept == c.specialization)) {
                relevant_sections.push_back(si);
            }
        }
        for (int si : relevant_sections) {
            for (int inst = 0; inst < c.lecSlots; ++inst) sessions.push_back({ "Lecture", c.code, si, inst });
            for (int inst = 0; inst < c.tutSlots; ++inst) sessions.push_back({ "Tutorial", c.code, si, inst });
            for (int inst = 0; inst < c.labSlots; ++inst) sessions.push_back({ "Lab", c.code, si, inst });
        }
    }

    assignments.resize(sessions.size(), { -1, -1, -1 });

    if (solve(0)) {
        print_timetable();
    }
    else {
        cout << "No feasible timetable found without conflicts." << endl;
    }

    return 0;
}