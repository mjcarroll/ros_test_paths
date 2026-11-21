#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include "rules_cc/cc/runfiles/runfiles.h"
int main() {
  using rules_cc::cc::runfiles::Runfiles;
  const char * names[] = {"RUNFILES_DIR", "RUNFILES_MANIFEST_FILE", "TEST_SRCDIR", "TEST_WORKSPACE", "TEST_UNDECLARED_OUTPUTS_DIR"};
  for (auto n : names) {const char * v = std::getenv(n); std::printf("%s=%s\n", n, v ? v : "(unset)");}
#ifdef BAZEL_CURRENT_REPOSITORY
  std::printf("BAZEL_CURRENT_REPOSITORY=[%s]\n", BAZEL_CURRENT_REPOSITORY);
#else
  std::printf("BAZEL_CURRENT_REPOSITORY undefined\n");
#endif
  std::string err;
  std::unique_ptr<Runfiles> r(Runfiles::CreateForTest(BAZEL_CURRENT_REPOSITORY, &err));
  std::printf("create: %s err=[%s]\n", r ? "ok" : "null", err.c_str());
  if (r) {
    std::printf("rlocation(repo/package.xml)=[%s]\n", r->Rlocation(std::string(BAZEL_CURRENT_REPOSITORY) + "/package.xml").c_str());
    std::printf("rlocation(rcl_yaml_param_parser+/package.xml)=[%s]\n", r->Rlocation("rcl_yaml_param_parser+/package.xml").c_str());
  }
  return 0;
}
