#pragma once
#include <cstddef>
#include <string>

namespace hades::storage {
// Android/macOS storage adaptation, independent of the original file formats.
// Publish a complete private file with a unique temporary file on the same
// filesystem. Success requires both file contents and the directory rename to
// have been synchronized. A failure after rename can leave the complete new file
// visible; it is never safe to interpret false as "the old file is unchanged".
// Callers serialize writes to the same destination and own backup policy.
bool publishFile(const std::string& path,const void* data,size_t size,std::string& error);
}
