#pragma once

namespace flowforge::cli {

// Entry point for the `flowforge` command line. Returns a process exit code:
//   0  success
//   1  command ran but reported failure (validation errors, pipeline failed,
//      run not found, ...)
//   2  usage error (unknown command / bad arguments)
int main(int argc, char** argv);

}  // namespace flowforge::cli
