#pragma once

#include "seastarx.h"

#include <seastar/core/file.hh>
#include <seastar/core/future.hh>
#include <seastar/core/lowres_clock.hh>
#include <seastar/core/sstring.hh>
#include <seastar/core/timer.hh>

#include <smf/human_bytes.h>
#include <smf/macros.h>

#include <cstdint>
#include <set>

class wal_nstpidx_repair {
public:
    struct item {
        int64_t epoch;
        int64_t term;
        int64_t size;
        sstring filename;
        lowres_system_clock::time_point modified;
    };
    struct comparator {
        bool operator()(const item& x, const item& y) const {
            return x.epoch < y.epoch;
        }
    };
    using set_t = std::set<item, comparator>;

    /// \brief main api
    static future<set_t> repair(sstring dir);

    SMF_DISALLOW_COPY_AND_ASSIGN(wal_nstpidx_repair);

private:
    explicit wal_nstpidx_repair(sstring workdir)
      : work_dir(workdir) {
    }

    /// \brief callback from directory_walker::walk
    future<> visit(directory_entry de);

private:
    const sstring work_dir;
    set_t _files;
};

namespace std {
inline ostream& operator<<(ostream& o, const wal_nstpidx_repair::item& i) {
    return o << "wal_nstpidx_repair::item{epoch:" << i.epoch
             << ", size:" << smf::human_bytes(i.size) << "(" << i.size
             << "), filename:" << i.filename << "}";
}
} // namespace std
