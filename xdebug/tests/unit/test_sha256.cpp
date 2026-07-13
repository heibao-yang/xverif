#include "core/common/sha256.h"

#include <cassert>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

int main() {
    const char* path = "/tmp/xdebug-test-sha256-input";
    FILE* output = std::fopen(path, "wb");
    assert(output);
    std::fputs("abc", output);
    std::fclose(output);
    std::string digest, error;
    assert(xdebug_core::sha256_file(path, digest, error));
    assert(digest == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::remove(path);
    const char* directory = "/tmp/xdebug-test-sha256-tree";
    mkdir(directory, 0700);
    std::string member = std::string(directory) + "/member";
    output = std::fopen(member.c_str(), "wb");
    assert(output); std::fputs("one", output); std::fclose(output);
    std::string first_tree;
    assert(xdebug_core::sha256_directory_tree(directory, first_tree, error));
    output = std::fopen(member.c_str(), "wb");
    assert(output); std::fputs("two", output); std::fclose(output);
    std::string second_tree;
    assert(xdebug_core::sha256_directory_tree(directory, second_tree, error));
    assert(first_tree != second_tree);
    std::remove(member.c_str()); rmdir(directory);
    return 0;
}
