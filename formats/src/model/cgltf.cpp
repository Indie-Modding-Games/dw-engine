// The one translation unit that compiles cgltf's reader and writer, which are single-header libraries. The writer's
// header includes the reader's again, so the reader's implementation must be off by then.
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#undef CGLTF_IMPLEMENTATION

#define CGLTF_WRITE_IMPLEMENTATION
#include <cgltf_write.h>
