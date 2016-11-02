//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Lukasz Janyst <ljanyst@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "ccsh_custom_user_interface.hpp"

#include "cling/Interpreter/Interpreter.h"
#include "cling/MetaProcessor/MetaProcessor.h"

#include "clang/Basic/LangOptions.h"
#include "clang/Frontend/CompilerInstance.h"

#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;

#include <iostream>
#include <fstream>
#include <vector>
#include <string>

#if defined(WIN32) && defined(_MSC_VER)
#include <crtdbg.h>
#endif

std::vector<const char*> add_args(int argc, const char * const * argv, std::vector<std::string> const& params)
{
  std::vector<const char*> args;
  args.reserve(argc - 1 + params.size());

  args.push_back(argv[0]);

  for(const auto& param : params)
    args.push_back(param.c_str());

  for(int i = 1; i <= argc; ++i)
    args.push_back(argv[i]);

  return args;
}

int main( int argc, char **argv ) {

#if defined(_WIN32) && defined(_MSC_VER)
  // Suppress error dialogs to avoid hangs on build nodes.
  // One can use an environment variable (Cling_GuiOnAssert) to enable
  // the error dialogs.
  const char *EnablePopups = getenv("Cling_GuiOnAssert");
  if (EnablePopups == nullptr || EnablePopups[0] == '0') {
    ::_set_error_mode(_OUT_TO_STDERR);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  }
#endif

  // TODO: find a better way to specify path
  const char* llvmdir = "/opt/cling";

  fs::path p = fs::current_path() / fs::path(argv[0]);
  p = p.parent_path();

  std::vector<std::string> params = {
    "-std=c++14",
    "-L" + (p / "lib").string(),                // -Llib
    "-l", "ccsh_lib",                           // -l ccsh_lib
    "-l",  (p / "ui/clingrc.hpp").string(),     // -l ui/clingrc.hpp
    "-I" + (p / "include").string(),            // -Iinclude
    "-I" + (p / "wrappers").string(),           // -Iwrappers  
  };

  auto args = add_args(argc, argv, params);

  // Set up the interpreter
  cling::Interpreter interp(args.size() - 1, args.data(), llvmdir);
  if (interp.getOptions().Help) {
    return 0;
  }

  clang::CompilerInstance* CI = interp.getCI();
  interp.AddIncludePath(".");

  for (size_t I = 0, N = interp.getOptions().LibsToLoad.size(); I < N; ++I) {
    interp.loadFile(interp.getOptions().LibsToLoad[I]);
  }


  // Interactive means no input (or one input that's "-")
  std::vector<std::string>& Inputs = interp.getOptions().Inputs;
  bool Interactive = Inputs.empty() || (Inputs.size() == 1
                                        && Inputs[0] == "-");

  ccsh::ui::custom_user_interface ui(interp);
  // If we are not interactive we're supposed to parse files
  if (!Interactive) {
    for (size_t I = 0, N = Inputs.size(); I < N; ++I) {
      std::string cmd;
      cling::Interpreter::CompilationResult compRes;
      if (!interp.lookupFileOrLibrary(Inputs[I]).empty()) {
        std::ifstream infile(interp.lookupFileOrLibrary(Inputs[I]));
        std::string line;
        std::getline(infile, line);
        if (line[0] == '#' && line[1] == '!') {
          // TODO: Check whether the filename specified after #! is the current
          // executable.
          while(std::getline(infile, line)) {
            ui.getMetaProcessor()->process(line.c_str(), compRes, 0);
          }
          continue;
        }
        else
          cmd += ".x ";
      }
      cmd += Inputs[I];
      ui.getMetaProcessor()->process(cmd.c_str(), compRes, 0);
    }
  }
  else {
    ui.run_interactively();
  }

  bool ret = CI->getDiagnostics().getClient()->getNumErrors();

  // if we are running with -verify a reported has to be returned as unsuccess.
  // This is relevant especially for the test suite.
  if (CI->getDiagnosticOpts().VerifyDiagnostics) {
    // If there was an error that came from the verifier we must return 1 as
    // an exit code for the process. This will make the test fail as expected.
    clang::DiagnosticConsumer* client = CI->getDiagnostics().getClient();
    client->EndSourceFile();
    ret = client->getNumErrors();

    // The interpreter expects BeginSourceFile/EndSourceFiles to be balanced.
    client->BeginSourceFile(CI->getLangOpts(), &CI->getPreprocessor());
  }

  return ret;
}
