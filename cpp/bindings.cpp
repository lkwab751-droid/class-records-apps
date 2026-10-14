// bindings.cpp
// Exposes cm::ClassManager to JavaScript through Emscripten's embind.
// The file is empty for native builds, so `g++ cpp/*.cpp` will not choke on it.
#ifdef __EMSCRIPTEN__

#include <emscripten/bind.h>

#include "class_manager.h"

using namespace emscripten;

EMSCRIPTEN_BINDINGS(class_manager) {
    class_<cm::ClassManager>("ClassManager")
        .constructor<>()
        .function("clear", &cm::ClassManager::clear)
        .class_function("errorMessage", &cm::ClassManager::errorMessage)
        // roster
        .function("addStudent", &cm::ClassManager::addStudent)
        .function("editStudent", &cm::ClassManager::editStudent)
        .function("deleteStudent", &cm::ClassManager::deleteStudent)
        .function("sortRoster", &cm::ClassManager::sortRoster)
        // assignments + grades
        .function("addAssignment", &cm::ClassManager::addAssignment)
        .function("editAssignment", &cm::ClassManager::editAssignment)
        .function("deleteAssignment", &cm::ClassManager::deleteAssignment)
        .function("setGrade", &cm::ClassManager::setGrade)
        // attendance
        .function("addDay", &cm::ClassManager::addDay)
        .function("deleteDay", &cm::ClassManager::deleteDay)
        .function("setAttendance", &cm::ClassManager::setAttendance)
        .function("markDayAllPresent", &cm::ClassManager::markDayAllPresent)
        // calculations + export
        .function("classAverage", &cm::ClassManager::classAverage)
        .function("classGpa", &cm::ClassManager::classGpa)
        .function("toJson", &cm::ClassManager::toJson)
        .function("toCsv", &cm::ClassManager::toCsv);
}

#endif  // __EMSCRIPTEN__
