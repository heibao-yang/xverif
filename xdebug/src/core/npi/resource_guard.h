#pragma once

#include "common/unique_resource.h"

#include "npi.h"
#include "npi_fsdb.h"
#include "npi_hdl.h"

namespace xdebug {

struct NpiHandleReleaser {
    void operator()(npiHandle handle) const {
        if (handle) npi_release_handle(handle);
    }
};

struct FsdbScopeIterReleaser {
    void operator()(npiFsdbScopeIter iter) const {
        if (iter) npi_fsdb_iter_scope_stop(iter);
    }
};

struct FsdbVctReleaser {
    void operator()(npiFsdbVctHandle vct) const {
        if (vct) npi_fsdb_release_vct(vct);
    }
};

typedef xdebug_core::UniqueResource<npiHandle, NpiHandleReleaser> NpiHandleGuard;
typedef xdebug_core::UniqueResource<npiHandle, NpiHandleReleaser> NpiIteratorGuard;
typedef xdebug_core::UniqueResource<npiFsdbScopeIter, FsdbScopeIterReleaser> FsdbScopeIterGuard;
typedef xdebug_core::UniqueResource<npiFsdbVctHandle, FsdbVctReleaser> FsdbVctGuard;

} // namespace xdebug
