// class_manager.h
// Core data model for the Class Records app.
//
// Storage is deliberately array-based:
//   * roster_        std::array<Student, MAX_STUDENTS>       (names + IDs)
//   * grades_        double[MAX_STUDENTS][MAX_ASSIGNMENTS]   (static 2-D grade matrix)
//   * assignments_   std::array<Assignment, MAX_ASSIGNMENTS> (column metadata)
//   * present_       std::array<std::bitset<MAX_DAYS>, MAX_STUDENTS>  (attendance log)
//   * recorded_      std::array<std::bitset<MAX_DAYS>, MAX_STUDENTS>  (was the day marked?)
//   * std::vector    is used only for short-lived results (rankings, sort permutations)
#pragma once

#include <array>
#include <bitset>
#include <string>
#include <vector>

namespace cm {

constexpr int MAX_STUDENTS = 60;
constexpr int MAX_ASSIGNMENTS = 20;
constexpr int MAX_DAYS = 60;
constexpr double NO_GRADE = -1.0;  // sentinel stored in the grade matrix

// Result codes returned by mutating calls. Anything >= 0 is success
// (and, for add* calls, is the index of the new item).
enum Result {
    OK = 0,
    ERR_FULL = -1,
    ERR_INDEX = -2,
    ERR_DUPLICATE = -3,
    ERR_INVALID = -4
};

struct Student {
    std::string id;
    std::string name;
};

struct Assignment {
    std::string name;
    double maxPoints = 100.0;
    double weight = 1.0;  // relative weight; weights do not need to sum to 100
};

class ClassManager {
public:
    ClassManager();

    // ---- lifecycle -------------------------------------------------------
    void clear();
    static std::string errorMessage(int code);

    // ---- roster ----------------------------------------------------------
    int addStudent(const std::string& id, const std::string& name);
    int editStudent(int index, const std::string& id, const std::string& name);
    int deleteStudent(int index);
    // mode: 0 = name (A-Z), 1 = ID, 2 = final grade (high to low)
    int sortRoster(int mode);

    // ---- assignments (columns of the grade matrix) -----------------------
    int addAssignment(const std::string& name, double maxPoints, double weight);
    int editAssignment(int index, const std::string& name, double maxPoints, double weight);
    int deleteAssignment(int index);
    // A negative score clears the grade.
    int setGrade(int student, int assignment, double score);

    // ---- attendance ------------------------------------------------------
    int addDay(const std::string& label);
    int deleteDay(int day);
    // state: 1 = present, 0 = absent, -1 = not marked
    int setAttendance(int student, int day, int state);
    int markDayAllPresent(int day);

    // ---- calculations ----------------------------------------------------
    int studentCount() const { return studentCount_; }
    int assignmentCount() const { return assignmentCount_; }
    int dayCount() const { return dayCount_; }

    // Weighted percentage over the assignments the student has a grade for.
    // Returns NO_GRADE when nothing is graded yet.
    double studentPercent(int student) const;
    double classAverage() const;
    double classGpa() const;
    double assignmentAverage(int assignment) const;  // mean of score/max, in percent
    double studentAttendanceRate(int student) const;
    double dayAttendanceRate(int day) const;
    double classAttendanceRate() const;

    static char letterFor(double percent);
    static double gpaPointsFor(double percent);

    // Student indices ordered from highest to lowest final grade
    // (students with no grades come last).
    std::vector<int> rankedOrder() const;

    // ---- export ----------------------------------------------------------
    // Full snapshot (data + computed values) as JSON. Also used as the backup format.
    std::string toJson() const;
    // Gradebook as CSV (assignments, max points, weights, grades, final results).
    std::string toCsv() const;

private:
    bool validStudent(int i) const { return i >= 0 && i < studentCount_; }
    bool validAssignment(int i) const { return i >= 0 && i < assignmentCount_; }
    bool validDay(int i) const { return i >= 0 && i < dayCount_; }
    int findStudentById(const std::string& id) const;

    int studentCount_ = 0;
    int assignmentCount_ = 0;
    int dayCount_ = 0;

    std::array<Student, MAX_STUDENTS> roster_;
    std::array<Assignment, MAX_ASSIGNMENTS> assignments_;
    double grades_[MAX_STUDENTS][MAX_ASSIGNMENTS];
    std::array<std::string, MAX_DAYS> dayLabels_;
    std::array<std::bitset<MAX_DAYS>, MAX_STUDENTS> present_;
    std::array<std::bitset<MAX_DAYS>, MAX_STUDENTS> recorded_;
};

}  // namespace cm
