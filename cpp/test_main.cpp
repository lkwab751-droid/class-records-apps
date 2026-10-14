// test_main.cpp
// Native sanity checks for the core. Build and run with:
//   g++ -std=c++17 -Wall -Wextra cpp/class_manager.cpp cpp/test_main.cpp -o test && ./test
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "class_manager.h"

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);            \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

static bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }

int main() {
    using namespace cm;
    ClassManager c;

    // roster
    CHECK(c.addStudent("S1", "Zed") == 0);
    CHECK(c.addStudent("S2", "Amy") == 1);
    CHECK(c.addStudent("S3", "Bo") == 2);
    CHECK(c.addStudent("S2", "Dup") == ERR_DUPLICATE);
    CHECK(c.addStudent(" ", "Nobody") == ERR_INVALID);

    // assignments + weighted grades
    CHECK(c.addAssignment("Quiz", 20, 25) == 0);
    CHECK(c.addAssignment("Exam", 100, 75) == 1);
    CHECK(c.setGrade(0, 0, 20) == OK);   // 100%
    CHECK(c.setGrade(0, 1, 80) == OK);   // 80%  -> 0.25*100 + 0.75*80 = 85
    CHECK(near(c.studentPercent(0), 85.0));
    CHECK(c.setGrade(1, 1, 90) == OK);   // only the exam graded -> 90
    CHECK(near(c.studentPercent(1), 90.0));
    CHECK(c.studentPercent(2) < 0);      // nothing graded
    CHECK(near(c.classAverage(), 87.5));
    CHECK(near(c.classGpa(), 3.5));      // B (3.0) and A (4.0)
    CHECK(c.setGrade(0, 1, INFINITY) == ERR_INVALID);
    CHECK(c.addAssignment("Bad", 0, 1) == ERR_INVALID);

    // sort by name moves every parallel array together
    CHECK(c.sortRoster(0) == OK);
    CHECK(c.toJson().find("\"name\":\"Amy\"") < c.toJson().find("\"name\":\"Zed\""));
    CHECK(near(c.studentPercent(0), 90.0));  // Amy now first
    CHECK(near(c.studentPercent(2), 85.0));  // Zed now last

    // attendance
    CHECK(c.addDay("2026-10-12") == 0);
    CHECK(c.addDay("2026-10-13") == 1);
    CHECK(c.addDay("2026-10-14") == 2);
    CHECK(c.setAttendance(0, 0, 1) == OK);
    CHECK(c.setAttendance(0, 1, 0) == OK);
    CHECK(c.setAttendance(0, 2, 1) == OK);
    CHECK(near(c.studentAttendanceRate(0), 200.0 / 3.0));
    CHECK(c.markDayAllPresent(1) == OK);
    CHECK(near(c.dayAttendanceRate(1), 100.0));
    CHECK(c.deleteDay(1) == OK);          // removing the middle day shifts bits down
    CHECK(c.dayCount() == 2);
    CHECK(near(c.studentAttendanceRate(0), 100.0));
    CHECK(c.setAttendance(0, 5, 1) == ERR_INDEX);

    // delete assignment column and student row
    CHECK(c.deleteAssignment(0) == OK);   // drop the quiz
    CHECK(c.assignmentCount() == 1);
    CHECK(near(c.studentPercent(2), 80.0));  // Zed's exam only
    CHECK(c.deleteStudent(0) == OK);
    CHECK(c.studentCount() == 2);
    CHECK(c.rankedOrder().size() == 2);

    // exports contain what we expect
    const std::string json = c.toJson();
    CHECK(json.find("\"students\":[") != std::string::npos);
    CHECK(json.find("\"classAverage\":") != std::string::npos);
    const std::string csv = c.toCsv();
    CHECK(csv.rfind("ID,Name,Exam,Final %", 0) == 0);

    // limits
    c.clear();
    for (int i = 0; i < MAX_STUDENTS; ++i) CHECK(c.addStudent("ID" + std::to_string(i), "N") >= 0);
    CHECK(c.addStudent("extra", "N") == ERR_FULL);

    if (failures == 0) std::printf("All checks passed.\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
