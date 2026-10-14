// class_manager.cpp
#include "class_manager.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <utility>

namespace cm {

namespace {

// ---------- small string helpers ------------------------------------------

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

std::string quoted(const std::string& s) { return "\"" + jsonEscape(s) + "\""; }

std::string fmt(double v, const char* spec) {
    char buf[64];
    std::snprintf(buf, sizeof buf, spec, v);
    return buf;
}

// Negative values (NO_GRADE) become JSON null.
std::string numOrNull(double v, const char* spec = "%.2f") {
    return v < 0 ? "null" : fmt(v, spec);
}

std::string csvField(const std::string& s) {
    if (s.find_first_of(",\"\r\n") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + "\"";
}

}  // namespace

// ============================================================================
// lifecycle
// ============================================================================

ClassManager::ClassManager() { clear(); }

void ClassManager::clear() {
    studentCount_ = assignmentCount_ = dayCount_ = 0;
    roster_.fill(Student{});
    assignments_.fill(Assignment{});
    dayLabels_.fill(std::string{});
    present_.fill(std::bitset<MAX_DAYS>{});
    recorded_.fill(std::bitset<MAX_DAYS>{});
    for (int s = 0; s < MAX_STUDENTS; ++s)
        std::fill(grades_[s], grades_[s] + MAX_ASSIGNMENTS, NO_GRADE);
}

std::string ClassManager::errorMessage(int code) {
    switch (code) {
        case ERR_FULL:      return "Limit reached. Remove an item before adding another.";
        case ERR_INDEX:     return "That item no longer exists.";
        case ERR_DUPLICATE: return "A student with that ID already exists.";
        case ERR_INVALID:   return "Check the values you entered and try again.";
        default:            return "";
    }
}

int ClassManager::findStudentById(const std::string& id) const {
    for (int i = 0; i < studentCount_; ++i)
        if (roster_[i].id == id) return i;
    return -1;
}

// ============================================================================
// roster
// ============================================================================

int ClassManager::addStudent(const std::string& idIn, const std::string& nameIn) {
    const std::string id = trim(idIn), name = trim(nameIn);
    if (id.empty() || name.empty()) return ERR_INVALID;
    if (studentCount_ >= MAX_STUDENTS) return ERR_FULL;
    if (findStudentById(id) >= 0) return ERR_DUPLICATE;

    const int i = studentCount_++;
    roster_[i] = Student{id, name};
    std::fill(grades_[i], grades_[i] + MAX_ASSIGNMENTS, NO_GRADE);
    present_[i].reset();
    recorded_[i].reset();
    return i;
}

int ClassManager::editStudent(int index, const std::string& idIn, const std::string& nameIn) {
    if (!validStudent(index)) return ERR_INDEX;
    const std::string id = trim(idIn), name = trim(nameIn);
    if (id.empty() || name.empty()) return ERR_INVALID;
    const int existing = findStudentById(id);
    if (existing >= 0 && existing != index) return ERR_DUPLICATE;
    roster_[index] = Student{id, name};
    return OK;
}

int ClassManager::deleteStudent(int index) {
    if (!validStudent(index)) return ERR_INDEX;

    // Close the gap: shift every parallel array one slot toward the front.
    for (int i = index; i < studentCount_ - 1; ++i) {
        roster_[i] = std::move(roster_[i + 1]);
        std::copy(grades_[i + 1], grades_[i + 1] + MAX_ASSIGNMENTS, grades_[i]);
        present_[i] = present_[i + 1];
        recorded_[i] = recorded_[i + 1];
    }
    const int last = --studentCount_;
    roster_[last] = Student{};
    std::fill(grades_[last], grades_[last] + MAX_ASSIGNMENTS, NO_GRADE);
    present_[last].reset();
    recorded_[last].reset();
    return OK;
}

int ClassManager::sortRoster(int mode) {
    if (mode < 0 || mode > 2) return ERR_INVALID;

    // Sort a permutation of indices, then apply it to every parallel array.
    std::vector<int> perm(studentCount_);
    std::iota(perm.begin(), perm.end(), 0);

    std::vector<double> pct(studentCount_);
    for (int i = 0; i < studentCount_; ++i) pct[i] = studentPercent(i);

    std::stable_sort(perm.begin(), perm.end(), [&](int a, int b) {
        switch (mode) {
            case 0:  return lower(roster_[a].name) < lower(roster_[b].name);
            case 1:  return lower(roster_[a].id) < lower(roster_[b].id);
            default:
                if (pct[a] < 0 || pct[b] < 0) return pct[a] >= 0 && pct[b] < 0;
                return pct[a] > pct[b];
        }
    });

    const std::array<Student, MAX_STUDENTS> rosterCopy = roster_;
    const auto presentCopy = present_;
    const auto recordedCopy = recorded_;
    std::vector<double> gradeCopy(static_cast<size_t>(MAX_STUDENTS) * MAX_ASSIGNMENTS);
    for (int s = 0; s < MAX_STUDENTS; ++s)
        std::copy(grades_[s], grades_[s] + MAX_ASSIGNMENTS, gradeCopy.begin() + s * MAX_ASSIGNMENTS);

    for (int to = 0; to < studentCount_; ++to) {
        const int from = perm[to];
        roster_[to] = rosterCopy[from];
        present_[to] = presentCopy[from];
        recorded_[to] = recordedCopy[from];
        std::copy(gradeCopy.begin() + from * MAX_ASSIGNMENTS,
                  gradeCopy.begin() + (from + 1) * MAX_ASSIGNMENTS, grades_[to]);
    }
    return OK;
}

// ============================================================================
// assignments
// ============================================================================

int ClassManager::addAssignment(const std::string& nameIn, double maxPoints, double weight) {
    const std::string name = trim(nameIn);
    if (name.empty() || !std::isfinite(maxPoints) || !std::isfinite(weight) ||
        !(maxPoints > 0) || !(weight >= 0))
        return ERR_INVALID;
    if (assignmentCount_ >= MAX_ASSIGNMENTS) return ERR_FULL;

    const int a = assignmentCount_++;
    assignments_[a] = Assignment{name, maxPoints, weight};
    for (int s = 0; s < MAX_STUDENTS; ++s) grades_[s][a] = NO_GRADE;
    return a;
}

int ClassManager::editAssignment(int index, const std::string& nameIn, double maxPoints,
                                 double weight) {
    if (!validAssignment(index)) return ERR_INDEX;
    const std::string name = trim(nameIn);
    if (name.empty() || !std::isfinite(maxPoints) || !std::isfinite(weight) ||
        !(maxPoints > 0) || !(weight >= 0))
        return ERR_INVALID;
    assignments_[index] = Assignment{name, maxPoints, weight};
    return OK;
}

int ClassManager::deleteAssignment(int index) {
    if (!validAssignment(index)) return ERR_INDEX;

    // Remove a column: shift each student's row left over the gap.
    for (int s = 0; s < studentCount_; ++s) {
        std::copy(grades_[s] + index + 1, grades_[s] + assignmentCount_, grades_[s] + index);
        grades_[s][assignmentCount_ - 1] = NO_GRADE;
    }
    for (int a = index; a < assignmentCount_ - 1; ++a)
        assignments_[a] = std::move(assignments_[a + 1]);
    assignments_[--assignmentCount_] = Assignment{};
    return OK;
}

int ClassManager::setGrade(int student, int assignment, double score) {
    if (!validStudent(student) || !validAssignment(assignment)) return ERR_INDEX;
    if (!std::isfinite(score)) return ERR_INVALID;
    grades_[student][assignment] = score < 0 ? NO_GRADE : score;
    return OK;
}

// ============================================================================
// attendance
// ============================================================================

int ClassManager::addDay(const std::string& labelIn) {
    const std::string label = trim(labelIn);
    if (label.empty()) return ERR_INVALID;
    if (dayCount_ >= MAX_DAYS) return ERR_FULL;

    const int d = dayCount_++;
    dayLabels_[d] = label;
    for (int s = 0; s < MAX_STUDENTS; ++s) {
        present_[s].reset(d);
        recorded_[s].reset(d);
    }
    return d;
}

int ClassManager::deleteDay(int day) {
    if (!validDay(day)) return ERR_INDEX;

    // Remove one bit from each bitset: keep bits below `day`,
    // shift bits above it down by one.
    auto removeBit = [day](std::bitset<MAX_DAYS>& bits) {
        std::bitset<MAX_DAYS> low;
        for (int i = 0; i < day; ++i) low[i] = bits[i];
        const std::bitset<MAX_DAYS> high = (bits >> (day + 1)) << day;
        bits = low | high;
    };
    for (int s = 0; s < MAX_STUDENTS; ++s) {
        removeBit(present_[s]);
        removeBit(recorded_[s]);
    }
    for (int d = day; d < dayCount_ - 1; ++d) dayLabels_[d] = std::move(dayLabels_[d + 1]);
    dayLabels_[--dayCount_].clear();
    return OK;
}

int ClassManager::setAttendance(int student, int day, int state) {
    if (!validStudent(student) || !validDay(day)) return ERR_INDEX;
    switch (state) {
        case 1:  recorded_[student].set(day);   present_[student].set(day);   break;
        case 0:  recorded_[student].set(day);   present_[student].reset(day); break;
        case -1: recorded_[student].reset(day); present_[student].reset(day); break;
        default: return ERR_INVALID;
    }
    return OK;
}

int ClassManager::markDayAllPresent(int day) {
    if (!validDay(day)) return ERR_INDEX;
    for (int s = 0; s < studentCount_; ++s) {
        recorded_[s].set(day);
        present_[s].set(day);
    }
    return OK;
}

// ============================================================================
// calculations
// ============================================================================

double ClassManager::studentPercent(int student) const {
    if (!validStudent(student)) return NO_GRADE;
    double earned = 0.0, weightSum = 0.0;
    for (int a = 0; a < assignmentCount_; ++a) {
        const double g = grades_[student][a];
        if (g < 0) continue;  // not graded yet: leave it out rather than count it as zero
        const Assignment& as = assignments_[a];
        earned += (g / as.maxPoints) * as.weight;
        weightSum += as.weight;
    }
    return weightSum > 0 ? earned / weightSum * 100.0 : NO_GRADE;
}

double ClassManager::classAverage() const {
    double sum = 0.0;
    int n = 0;
    for (int s = 0; s < studentCount_; ++s) {
        const double p = studentPercent(s);
        if (p >= 0) { sum += p; ++n; }
    }
    return n ? sum / n : NO_GRADE;
}

double ClassManager::classGpa() const {
    double sum = 0.0;
    int n = 0;
    for (int s = 0; s < studentCount_; ++s) {
        const double p = studentPercent(s);
        if (p >= 0) { sum += gpaPointsFor(p); ++n; }
    }
    return n ? sum / n : NO_GRADE;
}

double ClassManager::assignmentAverage(int assignment) const {
    if (!validAssignment(assignment)) return NO_GRADE;
    double sum = 0.0;
    int n = 0;
    for (int s = 0; s < studentCount_; ++s) {
        const double g = grades_[s][assignment];
        if (g >= 0) { sum += g / assignments_[assignment].maxPoints * 100.0; ++n; }
    }
    return n ? sum / n : NO_GRADE;
}

double ClassManager::studentAttendanceRate(int student) const {
    if (!validStudent(student)) return NO_GRADE;
    const size_t marked = recorded_[student].count();
    return marked ? 100.0 * (present_[student] & recorded_[student]).count() / marked : NO_GRADE;
}

double ClassManager::dayAttendanceRate(int day) const {
    if (!validDay(day)) return NO_GRADE;
    int marked = 0, here = 0;
    for (int s = 0; s < studentCount_; ++s) {
        if (!recorded_[s][day]) continue;
        ++marked;
        if (present_[s][day]) ++here;
    }
    return marked ? 100.0 * here / marked : NO_GRADE;
}

double ClassManager::classAttendanceRate() const {
    size_t marked = 0, here = 0;
    for (int s = 0; s < studentCount_; ++s) {
        marked += recorded_[s].count();
        here += (present_[s] & recorded_[s]).count();
    }
    return marked ? 100.0 * here / marked : NO_GRADE;
}

char ClassManager::letterFor(double p) {
    if (p >= 90) return 'A';
    if (p >= 80) return 'B';
    if (p >= 70) return 'C';
    if (p >= 60) return 'D';
    return 'F';
}

double ClassManager::gpaPointsFor(double p) {
    switch (letterFor(p)) {
        case 'A': return 4.0;
        case 'B': return 3.0;
        case 'C': return 2.0;
        case 'D': return 1.0;
        default:  return 0.0;
    }
}

std::vector<int> ClassManager::rankedOrder() const {
    std::vector<int> order(studentCount_);
    std::iota(order.begin(), order.end(), 0);
    std::vector<double> pct(studentCount_);
    for (int i = 0; i < studentCount_; ++i) pct[i] = studentPercent(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        if (pct[a] < 0 || pct[b] < 0) return pct[a] >= 0 && pct[b] < 0;
        return pct[a] > pct[b];
    });
    return order;
}

// ============================================================================
// export
// ============================================================================

std::string ClassManager::toJson() const {
    std::string j;
    j.reserve(8192);

    j += "{\"version\":1,\"limits\":{\"maxStudents\":" + std::to_string(MAX_STUDENTS) +
         ",\"maxAssignments\":" + std::to_string(MAX_ASSIGNMENTS) +
         ",\"maxDays\":" + std::to_string(MAX_DAYS) + "}";

    // assignments
    j += ",\"assignments\":[";
    double totalWeight = 0.0;
    for (int a = 0; a < assignmentCount_; ++a) {
        const Assignment& as = assignments_[a];
        totalWeight += as.weight;
        int graded = 0;
        double high = -1, low = -1;
        for (int s = 0; s < studentCount_; ++s) {
            const double g = grades_[s][a];
            if (g < 0) continue;
            ++graded;
            high = high < 0 ? g : std::max(high, g);
            low = low < 0 ? g : std::min(low, g);
        }
        if (a) j += ',';
        j += "{\"name\":" + quoted(as.name) + ",\"maxPoints\":" + fmt(as.maxPoints, "%.10g") +
             ",\"weight\":" + fmt(as.weight, "%.10g") +
             ",\"average\":" + numOrNull(assignmentAverage(a)) +
             ",\"high\":" + numOrNull(high, "%.10g") + ",\"low\":" + numOrNull(low, "%.10g") +
             ",\"graded\":" + std::to_string(graded) + "}";
    }
    j += ']';

    // attendance days
    j += ",\"days\":[";
    for (int d = 0; d < dayCount_; ++d) {
        int marked = 0, here = 0;
        for (int s = 0; s < studentCount_; ++s) {
            if (!recorded_[s][d]) continue;
            ++marked;
            if (present_[s][d]) ++here;
        }
        if (d) j += ',';
        j += "{\"label\":" + quoted(dayLabels_[d]) + ",\"present\":" + std::to_string(here) +
             ",\"recorded\":" + std::to_string(marked) +
             ",\"rate\":" + numOrNull(dayAttendanceRate(d)) + "}";
    }
    j += ']';

    // students
    const std::vector<int> order = rankedOrder();
    std::vector<int> rank(studentCount_, 0);
    {
        // Competition ranking: ties share a rank (1, 2, 2, 4, ...).
        int prevRank = 0;
        double prevPct = -2;
        for (int pos = 0; pos < studentCount_; ++pos) {
            const int s = order[pos];
            const double p = studentPercent(s);
            if (p < 0) { rank[s] = 0; continue; }
            if (std::fabs(p - prevPct) > 1e-9) { prevRank = pos + 1; prevPct = p; }
            rank[s] = prevRank;
        }
    }

    std::array<int, 5> dist{};  // A B C D F
    double highest = -1, lowest = -1;
    int gradedStudents = 0;

    j += ",\"students\":[";
    for (int s = 0; s < studentCount_; ++s) {
        const double p = studentPercent(s);
        if (p >= 0) {
            ++gradedStudents;
            highest = highest < 0 ? p : std::max(highest, p);
            lowest = lowest < 0 ? p : std::min(lowest, p);
            const char L = letterFor(p);
            ++dist[L == 'A' ? 0 : L == 'B' ? 1 : L == 'C' ? 2 : L == 'D' ? 3 : 4];
        }
        const int marked = static_cast<int>(recorded_[s].count());
        const int here = static_cast<int>((present_[s] & recorded_[s]).count());

        if (s) j += ',';
        j += "{\"index\":" + std::to_string(s) + ",\"id\":" + quoted(roster_[s].id) +
             ",\"name\":" + quoted(roster_[s].name) + ",\"grades\":[";
        for (int a = 0; a < assignmentCount_; ++a) {
            if (a) j += ',';
            j += numOrNull(grades_[s][a], "%.10g");
        }
        j += "],\"attendance\":[";
        for (int d = 0; d < dayCount_; ++d) {
            if (d) j += ',';
            j += !recorded_[s][d] ? "null" : (present_[s][d] ? "1" : "0");
        }
        j += "],\"percent\":" + numOrNull(p);
        j += ",\"letter\":" + (p >= 0 ? "\"" + std::string(1, letterFor(p)) + "\"" : std::string("null"));
        j += ",\"gpa\":" + (p >= 0 ? fmt(gpaPointsFor(p), "%.1f") : std::string("null"));
        j += ",\"rank\":" + (rank[s] > 0 ? std::to_string(rank[s]) : std::string("null"));
        j += ",\"present\":" + std::to_string(here) + ",\"recorded\":" + std::to_string(marked) +
             ",\"attendanceRate\":" + numOrNull(studentAttendanceRate(s)) + "}";
    }
    j += ']';

    j += ",\"stats\":{\"studentCount\":" + std::to_string(studentCount_) +
         ",\"gradedCount\":" + std::to_string(gradedStudents) +
         ",\"classAverage\":" + numOrNull(classAverage()) +
         ",\"classGpa\":" + numOrNull(classGpa()) +
         ",\"highest\":" + numOrNull(highest) + ",\"lowest\":" + numOrNull(lowest) +
         ",\"attendanceRate\":" + numOrNull(classAttendanceRate()) +
         ",\"totalWeight\":" + fmt(totalWeight, "%.10g") +
         ",\"distribution\":{\"A\":" + std::to_string(dist[0]) + ",\"B\":" + std::to_string(dist[1]) +
         ",\"C\":" + std::to_string(dist[2]) + ",\"D\":" + std::to_string(dist[3]) +
         ",\"F\":" + std::to_string(dist[4]) + "}}}";
    return j;
}

std::string ClassManager::toCsv() const {
    std::string c;

    // Row 1: headers
    c += "ID,Name";
    for (int a = 0; a < assignmentCount_; ++a) c += "," + csvField(assignments_[a].name);
    c += ",Final %,Letter,GPA Points,Attendance %\r\n";

    // Row 2-3: assignment metadata (read back on import)
    c += "Max Points,";
    for (int a = 0; a < assignmentCount_; ++a) c += "," + fmt(assignments_[a].maxPoints, "%.10g");
    c += ",,,,\r\n";
    c += "Weight,";
    for (int a = 0; a < assignmentCount_; ++a) c += "," + fmt(assignments_[a].weight, "%.10g");
    c += ",,,,\r\n";

    // Student rows
    for (int s = 0; s < studentCount_; ++s) {
        const double p = studentPercent(s);
        c += csvField(roster_[s].id) + "," + csvField(roster_[s].name);
        for (int a = 0; a < assignmentCount_; ++a) {
            c += ',';
            if (grades_[s][a] >= 0) c += fmt(grades_[s][a], "%.10g");
        }
        c += ',';
        if (p >= 0) c += fmt(p, "%.2f");
        c += ',';
        if (p >= 0) c += letterFor(p);
        c += ',';
        if (p >= 0) c += fmt(gpaPointsFor(p), "%.1f");
        c += ',';
        const double att = studentAttendanceRate(s);
        if (att >= 0) c += fmt(att, "%.1f");
        c += "\r\n";
    }
    return c;
}

}  // namespace cm
