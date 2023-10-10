/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2023, DIFITEC GmbH
 * Author: Tilman Blumhagen <tilman.blumhagen@difitec.de>
 */

/** @file iiopp.h
 * @brief Public C++ interface
 *
 * @see @ref iiopp
*/

#pragma once

#include <iio.h>
#include <string>

#if __cplusplus < 201703L
#include <boost/optional.hpp>
#else
#include <optional>
#endif
#include <stdexcept>
#include <system_error>
#include <cassert>
#include <memory>

/** @brief Public C++ API
 *
 * This is a C++ wrapper for @ref iio.h
 *
 * It requires C++17 or C++11 with Boost (for <tt>boost::optional</tt>).
 *
 * It provides:
 *
 * - Classes that model the major types, providing member functions for easy usage.
 * - Uniform access to attributes of channels and devices (with their attr/debug_attr/buffer_attr variants).
 * - Error handling (error codes are checked and turned into exceptions).
 * - Functions that may return \c NULL for "no string" are explicit by returning an <tt>std::optional</tt>.
 * - Iterators for idiomatic access to devices of context and channels of devices.
 * - Iterators for attributes.
 * - Implicit conversion to the wrapped C-types, so C++ instances can easily be passed to the C-API.
 *
 * @warning All objects live in the @ref iiopp::Context that created them. When a context gets destroyed (when
 * the last <tt>std::shared_ptr</tt> to it releases it) all its child objects die as well. All types have
 * weak reference semantic and become invalid when the context gets destroyed.
 * Lifetime is managed by <tt>std::shared_ptr</tt>.
 *
 * In consequence all types are cheap to copy (at the cost of assigning a pointer and maybe an integer). Copies refer
 * to the same underlying object.
 *
 * See @ref iiopp-enum.cpp for an example.
 */
namespace iiopp
{

#if __cplusplus < 201703L
using boost::optional;
#else
using std::optional;
#endif

class Context;
class Device;
class Buffer;

/** @brief Non-owning immutable null terminated string
 *
 * Used for argument/return type for functions that expect/return a C-string (null terminated char-array).
 * Provides implicit conversion of std::string while still retaining efficient pass-through for <tt>char const*</tt>.
 * Only valid as long as the original string is valid.
 */
class cstr
{
    char const * const s;
public:
    cstr(std::string const & s) : s(s.c_str()){}
    cstr(char const * s) : s(s){assert(s);}

    char const * c_str() const {return s;}
    operator char const * () const {return s;}
};

/** @brief Thrown to report errors.
 */
class error : public std::system_error
{
public:
    using std::system_error::system_error;
};

/** @brief Common interface for attribute access
 */
class IAttr
{
public:
    virtual ~IAttr(){}

    virtual cstr name() const = 0;

    virtual size_t read(char * dst, size_t size) const = 0; // Flawfinder: ignore
    virtual bool read_bool() const = 0;
    virtual double read_double() const = 0;
    virtual long long read_longlong() const = 0;

    virtual size_t write(cstr src) = 0;
    virtual void write_bool(bool val) = 0;
    virtual void write_double(double val) = 0;
    virtual void write_longlong(long long val) = 0;

    operator bool () const {return read_bool();}
    operator double () const {return read_double();}
    operator long long () const {return read_longlong();}

    cstr operator = (cstr val){write(val); return val;}
    bool operator = (bool val){write_bool(val); return val;}
    double operator = (double val){write_double(val); return val;}
    long long operator = (long long val){write_longlong(val); return val;}
};

/** @brief Optional string, used for C-functions that return @c nullptr for "no value".
 */
typedef optional<cstr> optstr;

namespace impl
{

std::shared_ptr<Context> new_ctx(iio_context * ctx);

inline optstr opt(char const * s)
{
    return s ? optstr{{s}} : optstr{};
}

inline std::string err_str(int err)
{
    char buf[1024]; // Flawfinder: ignore
    iio_strerror(err, buf, sizeof(buf));
    return buf;
}

[[noreturn]] inline void err(int err, char const * ctx)
{
    assert(err > 0);
    throw error(err, std::generic_category(), ctx);
}

inline void check(int ret, char const * ctx)
{
    if (ret)
    {
        assert(ret < 0);
        err(-ret, ctx);
    }
}

template <class T>
T check_n(T n, char const * s)
{
    if (n < 0)
        impl::err(static_cast<int>(-n), s);
    return n;
}

/** @brief Serves as base class to implement a @c vector-like interface
 *
 * Implements begin(), end() and the index operator for a type @a container_T (which should be derived from it) with elements of type @a element_T.
 */
template <class container_T, class element_T>
class IndexedSequence
{
    container_T & _me() {return *static_cast<container_T*>(this);}
public:

    /** @brief A random access iterator for an @ref IndexedSequence
     */
    class Iterator
    {
        container_T & c;
        size_t i;
    public:
        typedef std::random_access_iterator_tag iterator_category;
        typedef element_T value_type;
        typedef std::ptrdiff_t difference_type;
        typedef element_T *pointer;
        typedef element_T &reference;
        Iterator(container_T &cont, size_t idx) : c(cont), i(idx) {assert(idx <= cont.size());}

        element_T operator*() const { return c[i]; }
        Iterator& operator++() { assert(i <= c.size()); ++i; return *this;}
        Iterator operator++(int) { Iterator tmp = *this; ++(*this); return tmp; }
        bool operator == (const Iterator& rhs) const { assert(&c == &rhs.c); return i == rhs.i; }
        bool operator != (const Iterator& rhs) const { assert(&c == &rhs.c); return i != rhs.i; }
        bool operator < (const Iterator& rhs) const { assert(&c == &rhs.c); return i < rhs.i; }
        bool operator > (const Iterator& rhs) const { assert(&c == &rhs.c); return i > rhs.i; }
        bool operator <= (const Iterator& rhs) const { assert(&c == &rhs.c); return i <= rhs.i; }
        bool operator >= (const Iterator& rhs) const { assert(&c == &rhs.c); return i >= rhs.i; }
        Iterator operator + (ssize_t x) const { return Iterator(c, i + x); }
        ssize_t operator - (Iterator rhs) const { assert(&c == &rhs.c); return i - rhs.i; }
    };

    Iterator begin() {return Iterator(_me(), 0);}
    Iterator end() {return Iterator(_me(), _me().size());}
};

template <class obj_T,
         ssize_t read_T(obj_T const *, char const *, char *, size_t),
         int read_bool_T(obj_T const *, char const *, bool *),
         int read_double_T(obj_T const *, char const *, double *),
         int read_longlong_T(obj_T const *, char const *, long long *),
         ssize_t write_T(obj_T const *, char const *, char const *),
         int write_bool_T(obj_T const *, char const *, bool),
         int write_double_T(obj_T const *, char const *, double),
         int write_longlong_T(obj_T const *, char const *, long long)
         >
class AttrT : public IAttr
{
    obj_T const * const _obj;
    cstr const _name;
public:

    AttrT(obj_T const * obj, cstr name) : _obj(obj), _name(name){assert(obj && name);}

    cstr name() const override {return _name;}

    size_t read(char * dst, size_t size) const override {return check_n(read_T(_obj, _name, dst, size), "iio_..._attr_read");} // Flawfinder: ignore
    bool read_bool() const override {bool val; check(read_bool_T(_obj, _name, &val), "iio_..._attr_read_bool"); return val;}
    double read_double() const override {double val; check(read_double_T(_obj, _name, &val), "iio_..._attr_read_double"); return val;}
    long long read_longlong() const override {long long val; check(read_longlong_T(_obj, _name, &val), "iio_..._attr_read_longlong"); return val;}

    size_t write(cstr src) override {return check_n(write_T(_obj, _name, src), "iio_..._attr_write");}
    void write_bool(bool val) override {check(write_bool_T(_obj, _name, val), "iio_..._attr_write_bool");}
    void write_double(double val) override {check(write_double_T(_obj, _name, val), "iio_..._attr_write_double");}
    void write_longlong(long long val) override {check(write_longlong_T(_obj, _name, val), "iio_..._attr_write_longlong");}
};

template <class obj_T, class attr_T, char const * find_attr_T(obj_T const *, char const *)>
optional<attr_T> attr(obj_T const * obj, cstr name)
{
    char const * s = find_attr_T(obj, name);
    if (s)
        return {attr_T(obj, s)};
    return {};
}

template <class obj_T, class attr_T, char const * get_attr_T(obj_T const *, unsigned int)>
optional<attr_T> attr(obj_T const * obj, unsigned int idx)
{
    char const * s = get_attr_T(obj, idx);
    if (s)
        return {attr_T(obj, s)};
    return {};
}

/** @brief Vector-like accessor for all attributes of an object
 */
#ifdef DOXYGEN
template <class obj_T, class attr_T>
class AttrSeqT : public IndexedSequence<AttrSeqT<obj_T, attr_T>, attr_T>
#else
template <class obj_T, class attr_T,
          unsigned int get_attrs_count_T(obj_T const *),
          char const * get_attr_T(obj_T const *, unsigned int),
          char const * find_attr_T(obj_T const *, char const *)
          >
class AttrSeqT : public IndexedSequence<AttrSeqT<obj_T, attr_T, get_attrs_count_T, get_attr_T, find_attr_T>, attr_T>
#endif
{
    obj_T const * const _obj;
public:
    AttrSeqT(obj_T const * obj) : _obj(obj){assert(obj);}

    /** @brief Count of attributes
     */
    size_t size() const {return get_attrs_count_T(_obj);}

    /** @brief Access by attribute index
     */
    attr_T operator [](size_t idx)
    {
        if (auto ret = attr<obj_T, attr_T, get_attr_T>(_obj, idx))
            return *ret;
        throw std::out_of_range("invalid attribute index");
    }

    /** @brief Access by attribute name
     */
    attr_T operator [](cstr name)
    {
        if (auto ret = attr<obj_T, attr_T, find_attr_T>(_obj, name))
            return *ret;
        throw std::out_of_range("invalid attribute name");
    }
};

} // namespace impl

/** @brief C++ wrapper for the @ref Channel C-API
 */
class Channel
{
    iio_channel * const p;
public:

    Channel() = delete;
    Channel(iio_channel * chan) : p(chan), attrs(chan){}
    operator iio_channel * () const {return p;}

#ifndef DOXYGEN
    typedef impl::AttrT<iio_channel,
        iio_channel_attr_read,
        iio_channel_attr_read_bool,
        iio_channel_attr_read_double,
        iio_channel_attr_read_longlong,
        iio_channel_attr_write,
        iio_channel_attr_write_bool,
        iio_channel_attr_write_double,
        iio_channel_attr_write_longlong
        > Attr;

    typedef impl::AttrSeqT<iio_channel, Attr,
        iio_channel_get_attrs_count,
        iio_channel_get_attr,
        iio_channel_find_attr
        > AttrSeq;
#else
    typedef IAttr Attr;
    typedef impl::AttrSeqT<Channel, Attr> AttrSeq;
#endif

    optional<Attr> attr(cstr name) {return impl::attr<iio_channel, Attr, iio_channel_find_attr>(p, name);}
    optional<Attr> attr(unsigned int idx) {return impl::attr<iio_channel, Attr, iio_channel_get_attr>(p, idx);}

    AttrSeq attrs;

    void disable() {iio_channel_disable(p);}
    void enable() {iio_channel_enable(p);}
    optstr find_attr(cstr name) {return impl::opt(iio_channel_find_attr(p, name));}
    unsigned int attrs_count() const {return iio_channel_get_attrs_count(p);}
    void * data() const {return iio_channel_get_data(p);}
    Device device();
    cstr id() const {return iio_channel_get_id(p);}
    iio_modifier modifier() const {return iio_channel_get_modifier(p);}
    optstr name() const { return impl::opt(iio_channel_get_name(p));}
    iio_chan_type type() const {return iio_channel_get_type(p);}
    bool is_enabled() const { return iio_channel_is_enabled(p);}
    bool is_output() const { return iio_channel_is_output(p);}
    bool is_scan_element() const { return iio_channel_is_scan_element(p);}
    size_t read(Buffer buffer, void * dst, size_t len) const; // Flawfinder: ignore
    size_t read_raw(Buffer buffer, void * dst, size_t len) const;
    void set_data(void * data){iio_channel_set_data(p, data);}
    size_t write(Buffer buffer, void const * src, size_t len);
    size_t write_raw(Buffer buffer, void const * src, size_t len);

    void convert(void * dst, void const * src) const {iio_channel_convert(p, dst, src);}
    void convert_inverse(void * dst, void const * src) const {iio_channel_convert_inverse(p, dst, src);}
    iio_data_format const * data_format() const {return iio_channel_get_data_format(p);}
    unsigned long index() const { return impl::check_n(iio_channel_get_index(p), "iio_channel_get_index");}
};

/** @brief C++ wrapper for the @ref Buffer C-API
 */
class Buffer
{
    iio_buffer * const p;
public:
    Buffer(iio_buffer * buffer) : p(buffer){}
    operator iio_buffer * () const {return p;}
    void cancel() {iio_buffer_cancel(p);}
    void * end() {return iio_buffer_end(p);}
    void * first(Channel channel){ return iio_buffer_first(p, channel);}
    ssize_t for_each(ssize_t (*callback)(const struct iio_channel *chn, void *src, size_t bytes, void *d), void *data){return iio_buffer_foreach_sample(p, callback, data);}
    void * data() {return iio_buffer_get_data(p);}
    Device device();
    int poll_fd() const {
        int const ret = iio_buffer_get_poll_fd(p);
        if (ret < 0)
            impl::err(-ret, "iio_buffer_get_poll_fd");
        return ret;
    }
    size_t push() const { return impl::check_n(iio_buffer_push(p), "iio_buffer_push");}
    size_t push_partial(size_t samples_count) const {return impl::check_n(iio_buffer_push_partial(p, samples_count), "iio_buffer_push_partial");}
    size_t refill() const { return impl::check_n(iio_buffer_refill(p), "iio_buffer_refill");}
    void set_blocking_mode(bool blocking){impl::check(iio_buffer_set_blocking_mode(p, blocking), "iio_buffer_set_blocking_mode");}
    void set_data(void * data){iio_buffer_set_data(p, data);}
    void * start() {return iio_buffer_start(p);}
    ptrdiff_t step() const {return iio_buffer_step(p);}
};

/** @brief C++ wrapper for the @ref Device C-API
 */
class Device : public impl::IndexedSequence<Device, Channel>
{
    iio_device * const p;
public:

    size_t size() const
    {
        return channels_count();
    }

    Channel operator [](size_t i)
    {
        assert(i < channels_count());
        return channel(i);
    }

#ifndef DOXYGEN
    typedef impl::AttrT<iio_device,
        iio_device_attr_read,
        iio_device_attr_read_bool,
        iio_device_attr_read_double,
        iio_device_attr_read_longlong,
        iio_device_attr_write,
        iio_device_attr_write_bool,
        iio_device_attr_write_double,
        iio_device_attr_write_longlong
        > Attr;

    typedef impl::AttrSeqT<iio_device, Attr,
        iio_device_get_attrs_count,
        iio_device_get_attr,
        iio_device_find_attr
        > AttrSeq;
#else
    typedef IAttr Attr;
    typedef impl::AttrSeqT<Channel, Attr> AttrSeq;
#endif
    optional<Attr> attr(cstr name) {return impl::attr<iio_device, Attr, iio_device_find_attr>(p, name);}
    optional<Attr> attr(unsigned int idx) {return impl::attr<iio_device, Attr, iio_device_get_attr>(p, idx);}

    AttrSeq attrs;

#ifndef DOXYGEN
    typedef impl::AttrT<iio_device,
        iio_device_debug_attr_read,
        iio_device_debug_attr_read_bool,
        iio_device_debug_attr_read_double,
        iio_device_debug_attr_read_longlong,
        iio_device_debug_attr_write,
        iio_device_debug_attr_write_bool,
        iio_device_debug_attr_write_double,
        iio_device_debug_attr_write_longlong
        > DebugAttr;

    typedef impl::AttrSeqT<iio_device, DebugAttr,
        iio_device_get_debug_attrs_count,
        iio_device_get_debug_attr,
        iio_device_find_debug_attr
        > DebugAttrSeq;
#else
    typedef IAttr DebugAttr;
    typedef impl::AttrSeqT<Channel, DebugAttr> DebugAttrSeq;
#endif

    optional<DebugAttr> debug_attr(cstr name) {return impl::attr<iio_device, DebugAttr, iio_device_find_debug_attr>(p, name);}
    optional<DebugAttr> debug_attr(unsigned int idx) {return impl::attr<iio_device, DebugAttr, iio_device_get_debug_attr>(p, idx);}

    DebugAttrSeq debug_attrs;

#ifndef DOXYGEN
    typedef impl::AttrT<iio_device,
        iio_device_buffer_attr_read,
        iio_device_buffer_attr_read_bool,
        iio_device_buffer_attr_read_double,
        iio_device_buffer_attr_read_longlong,
        iio_device_buffer_attr_write,
        iio_device_buffer_attr_write_bool,
        iio_device_buffer_attr_write_double,
        iio_device_buffer_attr_write_longlong
        > BufferAttr;

    typedef impl::AttrSeqT<iio_device, BufferAttr,
        iio_device_get_buffer_attrs_count,
        iio_device_get_buffer_attr,
        iio_device_find_buffer_attr
        > BufferAttrSeq;
#else
    typedef IAttr BufferAttr;
    typedef impl::AttrSeqT<Channel, BufferAttr> BufferAttrSeq;
#endif

    optional<BufferAttr> buffer_attr(cstr name) {return impl::attr<iio_device, BufferAttr, iio_device_find_buffer_attr>(p, name);}
    optional<BufferAttr> buffer_attr(unsigned int idx) {return impl::attr<iio_device, BufferAttr, iio_device_get_buffer_attr>(p, idx);}

    BufferAttrSeq buffer_attrs;

    Device() = delete;
    Device(iio_device * dev) : p(dev), attrs(dev), debug_attrs(dev), buffer_attrs(dev){}
    operator iio_device * () const {return p;}

    optstr find_attr(cstr name) const {return impl::opt(iio_device_find_attr(p, name));}
    optstr find_buffer_attr(cstr name) const {return impl::opt(iio_device_find_buffer_attr(p, name));}
    Channel find_channel(cstr name, bool output) const {return Channel{iio_device_find_channel(p, name, output)};}
    unsigned int attrs_count() const {return iio_device_get_attrs_count(p);}
    unsigned int buffer_attrs_count() const {return iio_device_get_buffer_attrs_count(p);}
    Channel channel(unsigned int idx) const {return Channel{iio_device_get_channel(p, idx)};}
    unsigned int channels_count() const {return iio_device_get_channels_count(p);}
    Context context();
    void * data() const {return iio_device_get_data(p);}
    cstr id() const { return iio_device_get_id(p);}
    optstr label() const { return impl::opt(iio_device_get_label(p));}
    optstr name() const { return impl::opt(iio_device_get_name(p));}
    Device trigger() const {iio_device const * ret; impl::check(iio_device_get_trigger(p, &ret), "iio_device_get_trigger"); return const_cast<iio_device*>(ret);}
    bool is_trigger() const {return iio_device_is_trigger(p);}
    void set_data(void * data){iio_device_set_data(p, data);}
    void set_kernel_buffers_count(unsigned int nb_buffers) {impl::check(iio_device_set_kernel_buffers_count(p, nb_buffers), "iio_device_set_kernel_buffers_count");}
    void set_trigger(iio_device const * trigger) {impl::check(iio_device_set_trigger(p, trigger), "iio_device_set_trigger");}
    std::shared_ptr<Buffer> create_buffer(size_t samples_count, bool cyclic)
    {
        iio_buffer * buffer = iio_device_create_buffer(p, samples_count, cyclic);
        if (!buffer)
            impl::err(errno, "iio_device_create_buffer");

        auto deleter = [](Buffer * buf) {
            if (buf)
            {
                iio_buffer_destroy(*buf);
                delete buf;
            }
        };

        return std::shared_ptr<Buffer>{new Buffer(buffer), deleter};
    }
};

/** @brief C++ wrapper for the @ref Context C-API
 */
class Context : public impl::IndexedSequence<Context, Device>
{
    iio_context * const p;
public:
    size_t size() const
    {
        return devices_count();
    }

    Device operator [](size_t i)
    {
        assert(i < devices_count());
        return device(i);
    }

    Context() = delete;
    Context(iio_context * ctx) : p(ctx){assert(ctx);}
    operator iio_context * () const {return p;}

    std::shared_ptr<Context> clone() const {
        iio_context * ctx = iio_context_clone(p);
        if (!ctx)
            impl::err(errno, "iio_context_clone");

        return impl::new_ctx(ctx);
    }

    Device find_device(cstr name) const {return iio_context_find_device(p, name);}
    std::pair<cstr, cstr> attr(unsigned int idx) const
    {
        char const * name, * value;
        impl::check(iio_context_get_attr(p, idx, &name, &value), "iio_context_get_attr");
        return {name, value};
    }
    optstr attr_value(cstr name) const {return impl::opt(iio_context_get_attr_value(p, name));}
    unsigned int attrs_count() const {return iio_context_get_attrs_count(p);}
    cstr description() const {return iio_context_get_description(p);}
    Device device(unsigned int idx) const
    {
        return Device{iio_context_get_device(p, idx)};
    }
    unsigned int devices_count() const {return iio_context_get_devices_count(p);}
    cstr name() const {return iio_context_get_name(p);}

    struct Version
    {
        unsigned int major, minor;
        std::string git_tag;
    };

    Version version() const {
        Version ver;
        char git_tag[8]; // Flawfinder: ignore
        impl::check(iio_context_get_version(p, &ver.major, &ver.minor, git_tag), "iio_context_get_version");
        ver.git_tag = git_tag;
        return ver;
    }

    cstr xml() const {return iio_context_get_xml(p);}
    void set_timeout(unsigned int timeout_ms){impl::check(iio_context_set_timeout(p, timeout_ms), "iio_context_set_timeout");}
};

inline Context Device::context(){return const_cast<iio_context*>(iio_device_get_context(p));}
inline Device Channel::device() {return const_cast<iio_device*>(iio_channel_get_device(p));}
inline Device Buffer::device()  {return const_cast<iio_device*>(iio_buffer_get_device(p));}
inline size_t Channel::read(Buffer buffer, void * dst, size_t len) const {return iio_channel_read(p, buffer, dst, len);} // Flawfinder: ignore
inline size_t Channel::read_raw(Buffer buffer, void * dst, size_t len) const {return iio_channel_read_raw(p, buffer, dst, len);}
inline size_t Channel::write(Buffer buffer, void const * src, size_t len) {return iio_channel_write(p, buffer, src, len);}
inline size_t Channel::write_raw(Buffer buffer, void const * src, size_t len) {return iio_channel_write_raw(p, buffer, src, len);}

namespace impl
{
inline std::shared_ptr<Context> new_ctx(iio_context * ctx)
{
    assert(ctx);

    auto deleter = [](Context * ctx) {
        if (ctx)
        {
            iio_context_destroy(*ctx);
            delete ctx;
        }
    };

    return std::shared_ptr<Context>{new Context(ctx), deleter};
}
} // namespace impl


/** @brief C++ wrapper for @ref iio_create_context_from_uri
 */
inline std::shared_ptr<Context> create_from_uri(cstr uri)
{
    iio_context * ctx = iio_create_context_from_uri(uri);
    if (!ctx)
        impl::err(errno, "iio_create_context_from_uri");

    return impl::new_ctx(ctx);
}

/** @brief C++ wrapper for @ref iio_create_default_context
 */
inline std::shared_ptr<Context> create_default_context()
{
    iio_context * ctx = iio_create_default_context();
    if (!ctx)
        impl::err(errno, "iio_create_default_context");

    return impl::new_ctx(ctx);
}

/** @brief C++ wrapper for @ref iio_create_local_context
 */
inline std::shared_ptr<Context> create_local_context()
{
    iio_context * ctx = iio_create_local_context();
    if (!ctx)
        impl::err(errno, "iio_create_local_context");

    return impl::new_ctx(ctx);
}

/** @brief C++ wrapper for @ref iio_create_network_context
 */
inline std::shared_ptr<Context> create_network_context(cstr host)
{
    iio_context * ctx = iio_create_network_context(host);
    if (!ctx)
        impl::err(errno, "iio_create_network_context");

    return impl::new_ctx(ctx);
}

/** @brief C++ wrapper for @ref iio_create_xml_context
 */
inline std::shared_ptr<Context> create_xml_context(cstr xml_file)
{
    iio_context * ctx = iio_create_xml_context(xml_file);
    if (!ctx)
        impl::err(errno, "iio_create_xml_context");

    return impl::new_ctx(ctx);
}

/** @brief C++ wrapper for @ref iio_create_xml_context_mem
 */
inline std::shared_ptr<Context> create_xml_context_mem(char const * xml, size_t len)
{
    iio_context * ctx = iio_create_xml_context_mem(xml, len);
    if (!ctx)
        impl::err(errno, "iio_create_xml_context_mem");

    return impl::new_ctx(ctx);
}

class ContextInfo
{
    iio_context_info const * const p;
public:
    ContextInfo() = delete;
    ContextInfo(iio_context_info const * i) : p(i){assert(i);}
    operator iio_context_info const * () const {return p;}

    cstr description() const {return iio_context_info_get_description(p);}
    cstr uri() const {return iio_context_info_get_uri(p);}
};

class ScanContext
{
    iio_scan_context * const p;
public:
    ScanContext() = delete;
    ScanContext(iio_scan_context * ctx) : p(ctx){assert(ctx);}
    operator iio_scan_context * () const {return p;}

    class InfoList : public impl::IndexedSequence<InfoList, ContextInfo>
    {
        iio_context_info ** const p;
        size_t const n;
    public:
        InfoList() = delete;
        InfoList(iio_context_info ** p, size_t n) : p(p), n(n){assert(p);}
        operator iio_context_info ** () const {return p;}

        size_t size() const {return n;}

        ContextInfo operator [] (size_t i) const
        {
            if (i >= n)
                throw std::out_of_range("invalid info index");

            return ContextInfo(p[i]);
        }
    };

    std::shared_ptr<InfoList> info_list() const
    {
        iio_context_info ** lst;
        ssize_t const n = iio_scan_context_get_info_list(p, &lst);
        if (n < 0)
            impl::err(static_cast<int>(-n), "iio_scan_context_get_info_list");


        auto deleter = [](InfoList * lst) {
            if (lst)
            {
                iio_context_info_list_free(*lst);
                delete lst;
            }
        };

        return std::shared_ptr<InfoList>{new InfoList(lst, n), deleter};
    }
};

inline std::shared_ptr<ScanContext> create_scan_context(optstr backend, int flags)
{
    iio_scan_context * ctx = iio_create_scan_context(backend ? static_cast<char const*>(*backend) : nullptr, flags);
    if (!ctx)
        impl::err(errno, "iio_create_scan_context");

    auto deleter = [](ScanContext * ctx) {
        if (ctx)
        {
            iio_scan_context_destroy(*ctx);
            delete ctx;
        }
    };

    return std::shared_ptr<ScanContext>{new ScanContext(ctx), deleter};
}

class ScanBlock : public impl::IndexedSequence<ScanBlock, ContextInfo>
{
    iio_scan_block * const p;
    size_t const n;
public:
    ScanBlock() = delete;
    ScanBlock(iio_scan_block * blk) : p(blk), n(impl::check_n(iio_scan_block_scan(blk), "iio_scan_block_scan")){assert(blk);}
    operator iio_scan_block * () const {return p;}


        size_t size() const {return n;}

    ContextInfo operator [] (unsigned int i) const
    {
        if (iio_context_info * info = iio_scan_block_get_info(p, i))
            return ContextInfo(info);
        impl::err(errno, "iio_scan_block_get_info");
    }

};

inline std::shared_ptr<ScanBlock> create_scan_block(optstr backend, int flags)
{
    iio_scan_block * blk = iio_create_scan_block(backend ? static_cast<char const*>(*backend) : nullptr, flags);
    if (!blk)
        impl::err(errno, "iio_create_scan_block");

    auto deleter = [](ScanBlock * blk) {
        if (blk)
        {
            iio_scan_block_destroy(*blk);
            delete blk;
        }
    };

    return std::shared_ptr<ScanBlock>{new ScanBlock(blk), deleter};
}


/** @brief Reads the value of a channel by using "input" or "raw" attribute and applying "scale" and "offset" if available
 *
 * @see @c get_channel_value in the example @ref iio-monitor.c
 */
inline double value(Channel ch)
{
    {
        double val;
        if (!iio_channel_attr_read_double(ch, "input", &val))
            return val / 1000.;
    }

    double scale = 1;
    iio_channel_attr_read_double(ch, "scale", &scale);

    double offset = 0;
    iio_channel_attr_read_double(ch, "offset", &offset);

    double raw;
    impl::check(iio_channel_attr_read_double(ch, "raw", &raw), "reading raw value");

    return (raw + offset) * scale / 1000.;
}

} // namespace iiopp
