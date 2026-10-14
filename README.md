# Class Records

A class records manager for rosters, weighted grades, and attendance. The logic is C++ compiled to WebAssembly, so it runs entirely in the browser and deploys to GitHub Pages with no server.

- **Core (C++17):** `cpp/class_manager.{h,cpp}` holds every data structure and calculation.
- **Bindings:** `cpp/bindings.cpp` exposes the core to JavaScript with Emscripten's embind.
- **UI:** `public/index.html` (Tailwind via CDN) and `public/app.js` (vanilla JS) render what the core returns.
- **Data stays on the device.** The app saves to the browser's `localStorage`. Use Export to back up or move a class.

## How the arrays are used

| Data | C++ structure |
| --- | --- |
| Student roster (ID, name) | `std::array<Student, 60>` |
| Grades, students × assignments | static 2-D array `double grades_[60][20]` |
| Assignment metadata (name, points, weight) | `std::array<Assignment, 20>` |
| Attendance per student per day | `std::array<std::bitset<60>, 60>` for present, plus a second one for "was this day marked" |
| Rankings and sort permutations | `std::vector<int>` (short-lived results) |

Array algorithms in the core:

- **Delete a student:** shift every parallel array down one slot (`std::move`, `std::copy`).
- **Delete an assignment:** shift each row's columns left over the gap.
- **Delete an attendance day:** remove one bit from each bitset using masks and shifts.
- **Sort the roster:** `std::stable_sort` on an index permutation, then apply it to the roster, the grade matrix, and both bitset arrays.
- **Rankings:** `std::stable_sort` with ties sharing a rank.

Limits are constants at the top of `class_manager.h`: 60 students, 20 assignments, 60 class days.

### Grading rules

- A student's final grade is the weighted average of the assignments they have a score for. A blank score is left out, not counted as zero.
- Weights are relative. They do not need to sum to 100.
- Letters: A 90+, B 80+, C 70+, D 60+, otherwise F. GPA points are 4, 3, 2, 1, 0.
- Attendance rate is present ÷ marked. Unmarked cells are ignored.

## Project layout

```
cpp/
  class_manager.h      Data model and API
  class_manager.cpp    Arrays, calculations, JSON and CSV export
  bindings.cpp         Emscripten embind bindings (empty in native builds)
  test_main.cpp        Native tests for the core
public/
  index.html           Dashboard UI
  app.js               UI logic, import parsing, persistence
  wasm/                Build output (git-ignored)
build.sh               Compiles the core with emcc
.github/workflows/
  deploy.yml           Test, build, and deploy to GitHub Pages
```

## Run it locally

### 1. Install Emscripten (once)

```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install 3.1.64
./emsdk activate 3.1.64
source ./emsdk_env.sh      # run this in every new terminal
cd ..
```

### 2. Build the WebAssembly module

```bash
./build.sh
```

This is the command it runs:

```bash
emcc cpp/class_manager.cpp cpp/bindings.cpp \
  -std=c++17 -O2 -lembind \
  -s MODULARIZE=1 -s EXPORT_NAME=createClassModule \
  -s ENVIRONMENT=web -s ALLOW_MEMORY_GROWTH=1 -s STACK_SIZE=1048576 \
  -o public/wasm/class_manager.js
```

It writes `public/wasm/class_manager.js` and `class_manager.wasm`.

### 3. Serve the `public/` folder

Browsers will not load `.wasm` from `file://`, so use any static server:

```bash
python3 -m http.server 8000 --directory public
```

Open <http://localhost:8000>. The first visit loads a sample class so you can try everything. **Clear all** gives you an empty class.

### Test the C++ core without a browser

```bash
g++ -std=c++17 -Wall -Wextra cpp/class_manager.cpp cpp/test_main.cpp -o test && ./test
```

Add `-fsanitize=address,undefined` to check for memory errors.

## Deploy to GitHub Pages

1. Create a repository and push this project to the `main` branch.
2. In the repository, open **Settings → Pages**. Under **Build and deployment**, set **Source** to **GitHub Actions**.
3. Push to `main` (or run the workflow from the **Actions** tab). `.github/workflows/deploy.yml` will:
   1. compile and run the native C++ tests,
   2. install Emscripten and run `./build.sh`,
   3. upload `public/` and publish it.
4. Your site appears at `https://<your-username>.github.io/<repository-name>/`.

All asset paths in the app are relative, so it works under a repository subpath without changes. The compiled `.wasm` is built in CI and is not committed.

## Import and export

| Format | Contains | Notes |
| --- | --- | --- |
| **JSON** | Everything: roster, assignments, weights, grades, attendance, class name, plus computed results | Use this for full backups. Importing replaces the current class. |
| **CSV** | Gradebook only: roster, assignments, max points, weights, grades, plus final grade, letter, GPA points, and attendance % | Opens in Excel or Google Sheets. Importing a CSV reads the roster and grades; attendance is not in the CSV. |

CSV layout:

```
ID,Name,Quiz 1,Midterm,Final %,Letter,GPA Points,Attendance %
Max Points,,20,100,,,,
Weight,,10,30,,,,
S001,Amara Okafor,18,88,89.33,B,3.0,100.0
```

The `Max Points` and `Weight` rows are optional on import (defaults are 100 and 1). Columns from `Final %` onward are ignored on import, so you can import a file you edited in a spreadsheet. If a student ID is duplicated or a limit is exceeded, that row is skipped and the app tells you how many.

## Using the app

- **Roster:** add, edit, delete, and sort students.
- **Gradebook:** select an assignment heading to change its name, points, or weight. Type scores into the grid; clear a cell to remove a score.
- **Attendance:** add a class date, then select a cell to cycle present, absent, and not marked. **All present** marks the whole day at once.
- **Overview:** class average, class GPA, attendance rate, grade distribution, standings, and assignment averages.

## Notes

- The page loads Tailwind and the fonts from CDNs, so it needs an internet connection the first time. The class data itself never leaves the browser.
- The Tailwind CDN script is meant for prototypes. For a larger project, switch to the Tailwind CLI and commit the generated stylesheet.
- To change the limits, edit the constants in `class_manager.h`, rebuild, and rerun the tests.
