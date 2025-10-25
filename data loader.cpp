// timetable_scheduler.cpp
// Single-file C++17 program to load CSVs and solve a course-timetabling CSP
// Provided CSV filenames (place them next to the executable):
// Courses.csv, Instructor.csv, TAs.csv, Halls.csv, TimeSlots.csv, Sections.csv
// Build: g++ -std=c++17 timetable_scheduler.cpp -O2 -o scheduler
// Run: ./scheduler

#include <bits/stdc++.h>
using namespace std;

// --------------------------
// CSV parsing utility
// --------------------------
static vector<string> splitCSVLine(const string& line, char delim = ',') {
    vector<string> out;
    string cur;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '\"') {
            if (inQuotes && i + 1 < line.size() && line[i + 1] == '\"') {
                // escaped quote
                cur.push_back('\"');
                ++i;
            }
            else {
                inQuotes = !inQuotes;
            }
        }
        else if (c == delim && !inQuotes) {
            out.push_back(cur);
            cur.clear();
        }
        else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

vector<vector<string>> loadCSV(const string& filename) {
    ifstream f(filename);
    if (!f.is_open()) {
        cerr << "Failed to open " << filename << "\n";
        return {};
    }
    vector<vector<string>> rows;
    string line;
    while (getline(f, line)) {
        if (line.size() == 0) continue;
        auto r = splitCSVLine(line);
        rows.push_back(r);
    }
    return rows;
}

// --------------------------
// Data model
// --------------------------
struct Course {
    string id; // course code
    string name;
    // other attributes if present
};

struct TimeSlot {
    string id; // unique id
    string day; // e.g., Mon
    string start; // e.g., 08:00
    string end;   // e.g., 09:30
};

struct Room {
    string id;
    string type; // Classroom, ComputerLab, PHY_LAB, etc.
    int capacity;
};

struct Instructor {
    string id;
    string name;
    // qualifications: which courses or types (e.g., can teach LEC/TUT/LAB)
    unordered_set<string> qualCourses;
};

struct TA {
    string id;
    string name;
    unordered_set<string> qualRoles; // e.g., TUT, LAB
    unordered_set<string> qualCourses; // which courses they can assist
};

struct Section {
    string id; // e.g., CS101-1
    string courseId;
    int size; // number of students
    // which session types required: LEC, TUT, LAB
    vector<string> sessionTypes;
};

// Variable to assign: one session instance
struct SessionVar {
    string id; // unique var id
    string sectionId;
    string courseId;
    string type; // LEC, TUT, LAB
    int neededCapacity;
};

struct Assignment {
    string timeslotId;
    string roomId;
    string instructorId; // can be empty for TAs-only sessions
    string taId; // optional
};

// --------------------------
// Global data
// --------------------------
vector<Course> courses;
vector<TimeSlot> timeslots;
vector<Room> rooms;
vector<Instructor> instructors;
vector<TA> tas;
vector<Section> sections;
vector<SessionVar> variables;

unordered_map<string, Course*> courseById;
unordered_map<string, TimeSlot*> timeslotById;
unordered_map<string, Room*> roomById;
unordered_map<string, Instructor*> instrById;
unordered_map<string, TA*> taById;
unordered_map<string, Section*> sectionById;

// Domains per variable: list of possible assignments (timeslot, room, instr, ta)
using DomainItem = Assignment;
vector<vector<DomainItem>> domains;

// Current assignment
vector<optional<Assignment>> currentAssign;

// Conflict trackers for O(1) checks
unordered_map<string, unordered_set<string>> instrBusy; // timeslotId -> set of instrId
unordered_map<string, unordered_set<string>> taBusy;    // timeslotId -> set of taId
unordered_map<string, unordered_set<string>> roomBusy;  // timeslotId -> set of roomId
unordered_map<string, unordered_set<string>> sectionBusy; // timeslotId -> set of sectionId

// Quick utility
bool roomMatchesType(const Room& r, const string& requiredType) {
    string t = r.type;
    // normalize
    auto normalize = [&](string s) {
        for (auto& c : s) c = toupper((unsigned char)c);
        return s;
        };
    string R = normalize(t);
    string Q = requiredType;
    for (auto& c : Q) c = toupper((unsigned char)c);
    // simple matching rules
    if (Q.find("LAB") != string::npos) {
        return R.find("LAB") != string::npos || R.find("COMPUTER") != string::npos || R.find("PHY") != string::npos;
    }
    else if (Q.find("CLASS") != string::npos || Q.find("LECT") != string::npos) {
        return R.find("CLASS") != string::npos || R.find("LECT") != string::npos;
    }
    return R == Q;
}

// --------------------------
// Domain generation
// --------------------------
void buildDomains() {
    domains.clear();
    domains.resize(variables.size());
    for (size_t i = 0; i < variables.size(); ++i) {
        auto& v = variables[i];
        // determine required room type based on v.type
        string requiredRoomType = (v.type == "LAB") ? "LAB" : "CLASSROOM";
        // candidate rooms: capacity >= neededCapacity and matching type
        vector<Room*> candidateRooms;
        for (auto& r : rooms) { if (r.capacity >= v.neededCapacity && roomMatchesType(r, requiredRoomType)) candidateRooms.push_back(&r); }
        // candidate timeslots: all
        for (auto& ts : timeslots) {
            for (auto* rptr : candidateRooms) {
                // candidate instructors or TAs depending on session type
                if (v.type == "LEC") {
                    for (auto& ins : instructors) {
                        // If instructor qualified for the course or no qualification data -> allow
                        if (ins.qualCourses.empty() || ins.qualCourses.count(v.courseId)) {
                            domains[i].push_back({ ts.id, rptr->id, ins.id, string() });
                        }
                    }
                }
                else if (v.type == "TUT") {
                    // require a TA (or instructor) who can TUT this course
                    for (auto& t : tas) {
                        if ((t.qualRoles.empty() || t.qualRoles.count("TUT")) && (t.qualCourses.empty() || t.qualCourses.count(v.courseId))) {
                            // attach TA and choose an instructor optionally as supervisor: we'll allow empty instructor
                            domains[i].push_back({ ts.id, rptr->id, string(), t.id });
                        }
                    }
                    // also allow instructors to handle TUT if qualified
                    for (auto& ins : instructors) { if (ins.qualCourses.empty() || ins.qualCourses.count(v.courseId)) domains[i].push_back({ ts.id, rptr->id, ins.id, string() }); }
                }
                else if (v.type == "LAB") {
                    // labs need both a TA with LAB role or instructor
                    for (auto& t : tas) {
                        if ((t.qualRoles.empty() || t.qualRoles.count("LAB")) && (t.qualCourses.empty() || t.qualCourses.count(v.courseId))) {
                            domains[i].push_back({ ts.id, rptr->id, string(), t.id });
                        }
                    }
                    for (auto& ins : instructors) { if (ins.qualCourses.empty() || ins.qualCourses.count(v.courseId)) domains[i].push_back({ ts.id, rptr->id, ins.id, string() }); }
                }
                else {
                    // fallback: allow instructor or TA
                    for (auto& ins : instructors) { if (ins.qualCourses.empty() || ins.qualCourses.count(v.courseId)) domains[i].push_back({ ts.id, rptr->id, ins.id, string() }); }
                    for (auto& t : tas) { if (t.qualCourses.empty() || t.qualCourses.count(v.courseId)) domains[i].push_back({ ts.id, rptr->id, string(), t.id }); }
                }
            }
        }
        // shuffle domain to add variety
        std::shuffle(domains[i].begin(), domains[i].end(), std::mt19937(123 + i));
    }
}

// --------------------------
// Constraint checking & assign/unassign
// --------------------------

bool canAssignVar(int varIdx, const Assignment& a) {
    // Hard constraints:
    // - instructor not busy on this timeslot
    // - ta not busy
    // - room not busy
    // - section (students) not busy
    // - room capacity/type already ensured in domain generation

    // check room
    if (roomBusy[a.timeslotId].count(a.roomId)) return false;
    // check instructor
    if (!a.instructorId.empty()) {
        if (instrBusy[a.timeslotId].count(a.instructorId)) return false;
    }
    if (!a.taId.empty()) {
        if (taBusy[a.timeslotId].count(a.taId)) return false;
    }
    // section
    auto& v = variables[varIdx];
    if (sectionBusy[a.timeslotId].count(v.sectionId)) return false;
    return true;
}

void doAssign(int varIdx, const Assignment& a) {
    currentAssign[varIdx] = a;
    roomBusy[a.timeslotId].insert(a.roomId);
    if (!a.instructorId.empty()) instrBusy[a.timeslotId].insert(a.instructorId);
    if (!a.taId.empty()) taBusy[a.timeslotId].insert(a.taId);
    sectionBusy[a.timeslotId].insert(variables[varIdx].sectionId);
}

void undoAssign(int varIdx, const Assignment& a) {
    currentAssign[varIdx] = nullopt;
    roomBusy[a.timeslotId].erase(a.roomId);
    if (!a.instructorId.empty()) instrBusy[a.timeslotId].erase(a.instructorId);
    if (!a.taId.empty()) taBusy[a.timeslotId].erase(a.taId);
    sectionBusy[a.timeslotId].erase(variables[varIdx].sectionId);
}

// simple heuristic: choose var with smallest domain size unassigned
int selectUnassignedVar() {
    int best = -1; size_t bestSize = SIZE_MAX;
    for (size_t i = 0; i < variables.size(); ++i) {
        if (currentAssign[i].has_value()) continue;
        // count remaining legal options quickly (no deep forward checking)
        size_t legal = 0;
        for (auto& d : domains[i]) if (canAssignVar(i, d)) ++legal;
        if (legal < bestSize) { bestSize = legal; best = (int)i; }
    }
    return best;
}

bool backtrack(int depth = 0) {
    // check completion
    bool allAssigned = true;
    for (auto& x : currentAssign) if (!x.has_value()) { allAssigned = false; break; }
    if (allAssigned) return true;

    int var = selectUnassignedVar();
    if (var == -1) return false; // no viable var

    // try domain items
    for (auto& d : domains[var]) {
        if (!canAssignVar(var, d)) continue;
        doAssign(var, d);
        if (backtrack(depth + 1)) return true;
        undoAssign(var, d);
    }
    return false;
}

// --------------------------
// Loading functions for your CSV formats (expecting simple headers)
// The loader is flexible: if CSV has headers, we search columns by name.
// --------------------------
string getField(const vector<string>& row, const unordered_map<string, int>& map, const string& col) {
    auto it = map.find(col);
    if (it == map.end()) return string();
    int idx = it->second;
    if (idx < 0 || idx >= (int)row.size()) return string();
    return row[idx];
}

unordered_map<string, int> headerIndex(const vector<string>& headerRow) {
    unordered_map<string, int> m;
    for (size_t i = 0; i < headerRow.size(); ++i) {
        string s = headerRow[i];
        for (auto& c : s) c = tolower(c);
        m[s] = (int)i;
    }
    return m;
}

void loadAllCSV(const string& dir = ".") {
    // Courses.csv: id,name
    auto rows = loadCSV(dir + "/Courses.csv");
    if (!rows.empty()) {
        auto hdr = headerIndex(rows[0]);
        for (size_t i = 1; i < rows.size(); ++i) {
            Course c; c.id = getField(rows[i], hdr, "id"); c.name = getField(rows[i], hdr, "name");
            if (c.id.empty()) continue; courses.push_back(c); courseById[c.id] = &courses.back();
        }
    }
    // TimeSlots.csv: id,day,start,end
    rows = loadCSV(dir + "/TimeSlots.csv");
    if (!rows.empty()) {
        auto hdr = headerIndex(rows[0]);
        for (size_t i = 1; i < rows.size(); ++i) {
            TimeSlot t; t.id = getField(rows[i], hdr, "id"); t.day = getField(rows[i], hdr, "day"); t.start = getField(rows[i], hdr, "start"); t.end = getField(rows[i], hdr, "end");
            if (t.id.empty()) continue; timeslots.push_back(t); timeslotById[t.id] = &timeslots.back();
        }
    }
    // Halls.csv: id,type,capacity
    rows = loadCSV(dir + "/Halls.csv");
    if (!rows.empty()) {
        auto hdr = headerIndex(rows[0]);
        for (size_t i = 1; i < rows.size(); ++i) {
            Room r; r.id = getField(rows[i], hdr, "id"); r.type = getField(rows[i], hdr, "type"); string cap = getField(rows[i], hdr, "capacity"); r.capacity = cap.empty() ? 0 : stoi(cap);
            if (r.id.empty()) continue; rooms.push_back(r); roomById[r.id] = &rooms.back();
        }
    }
    // Instructor.csv: id,name,qualified_courses (semicolon separated)
    rows = loadCSV(dir + "/Instructor.csv");
    if (!rows.empty()) {
        auto hdr = headerIndex(rows[0]);
        for (size_t i = 1; i < rows.size(); ++i) {
            Instructor ins; ins.id = getField(rows[i], hdr, "id"); ins.name = getField(rows[i], hdr, "name");
            string q = getField(rows[i], hdr, "qualified_courses");
            if (!q.empty()) {
                string tmp; for (char c : q) { if (c == ';') { ins.qualCourses.insert(tmp); tmp.clear(); } else tmp.push_back(c); } if (!tmp.empty()) ins.qualCourses.insert(tmp);
            }
            if (ins.id.empty()) continue; instructors.push_back(ins); instrById[ins.id] = &instructors.back();
        }
    }
    // TAs.csv: id,name,roles (semicolon),qualified_courses (semicolon)
    rows = loadCSV(dir + "/TAs.csv");
    if (!rows.empty()) {
        auto hdr = headerIndex(rows[0]);
        for (size_t i = 1; i < rows.size(); ++i) {
            TA t; t.id = getField(rows[i], hdr, "id"); t.name = getField(rows[i], hdr, "name");
            string roles = getField(rows[i], hdr, "roles");
            if (!roles.empty()) { string tmp; for (char c : roles) { if (c == ';') { t.qualRoles.insert(tmp); tmp.clear(); } else tmp.push_back(c); } if (!tmp.empty()) t.qualRoles.insert(tmp); }
            string q = getField(rows[i], hdr, "qualified_courses");
            if (!q.empty()) { string tmp; for (char c : q) { if (c == ';') { t.qualCourses.insert(tmp); tmp.clear(); } else tmp.push_back(c); } if (!tmp.empty()) t.qualCourses.insert(tmp); }
            if (t.id.empty()) continue; tas.push_back(t); taById[t.id] = &tas.back();
        }
    }
    // Sections.csv: id,courseId,size,sessions (semicolon list like LEC;TUT;LAB)
    rows = loadCSV(dir + "/Sections.csv");
    if (!rows.empty()) {
        auto hdr = headerIndex(rows[0]);
        for (size_t i = 1; i < rows.size(); ++i) {
            Section s; s.id = getField(rows[i], hdr, "id"); s.courseId = getField(rows[i], hdr, "courseid");
            string sz = getField(rows[i], hdr, "size"); s.size = sz.empty() ? 0 : stoi(sz);
            string sess = getField(rows[i], hdr, "sessions"); if (!sess.empty()) { string tmp; for (char c : sess) { if (c == ';') { s.sessionTypes.push_back(tmp); tmp.clear(); } else tmp.push_back(c); } if (!tmp.empty()) s.sessionTypes.push_back(tmp); }
            if (s.id.empty()) continue; sections.push_back(s); sectionById[s.id] = &sections.back();
        }
    }
}

void buildVariablesFromSections() {
    variables.clear();
    for (auto& sec : sections) {
        for (auto& st : sec.sessionTypes) {
            SessionVar v;
            v.sectionId = sec.id;
            v.courseId = sec.courseId;
            v.type = st;
            v.neededCapacity = sec.size;
            v.id = sec.id + "::" + st;
            variables.push_back(v);
        }
    }
}

// --------------------------
// Output
// --------------------------
void printSolution() {
    cout << "=== Solution ===\n";
    for (size_t i = 0; i < variables.size(); ++i) {
        cout << variables[i].id << " => ";
        if (currentAssign[i].has_value()) {
            auto a = currentAssign[i].value();
            cout << "Timeslot=" << a.timeslotId << " Room=" << a.roomId;
            if (!a.instructorId.empty()) cout << " Instructor=" << a.instructorId;
            if (!a.taId.empty()) cout << " TA=" << a.taId;
            cout << "\n";
        }
        else cout << "UNASSIGNED\n";
    }
}

int main(int argc, char** argv) {
    string dir = "."; // you can pass a folder path as first arg
    if (argc > 1) dir = argv[1];
    cout << "Loading CSVs from: " << dir << "\n";
    loadAllCSV(dir);
    buildVariablesFromSections();
    if (variables.empty()) {
        cerr << "No variables to schedule. Check Sections.csv and session types.\n";
        return 1;
    }
    buildDomains();
    currentAssign.resize(variables.size());

    cout << "Variables: " << variables.size() << "\n";
    size_t totalDomain = 0; for (auto& d : domains) totalDomain += d.size();
    cout << "Average domain size: "; if (variables.size()) cout << (double)totalDomain / variables.size(); cout << "\n";

    bool ok = backtrack();
    if (ok) { printSolution(); return 0; }
    cerr << "Failed to find a complete schedule with the given hard constraints.\n";
    return 2;
}
