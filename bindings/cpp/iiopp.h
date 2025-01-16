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

#include <iio/iio.h>
#include <string>

#define IIOPP_HAVE_STD_OPIONAL (__cplusplus >= 201703L || _MSC_VER >= 1910)

#if IIOPP_HAVE_STD_OPIONAL
#include <optional>
#else
#include <boost/optional.hpp>
#endif
#include <stdexcept>
#include <system_error>
#include <cassert>
#include <type_traits>

/** @brief Public C++ API
 *
 * This is a C++ wrapper for @ref iio.h
 *
 * It requires C++17 or C++11 with Boost (for <tt>boost::optional</tt>).
 *
 * It provides:
 *
 * - Classes that model the major types, providing member functions for easy usage.
 * - Error handling (errors are checked and turned into exceptions).
 * - Simplified resource management via smart pointers.
 * - Functions that may return \c NULL for "no string" or "no object" are explicit by returning an <tt>std::optional</tt>.
 * - Iterators and array subscription operator for idiomatic access to sequences.
 * - Implicit conversion to the wrapped C-types, so C++ instances can easily be passed to the C-API. Arguments C++-API take the raw C-types.
 *
 * @warning All objects live in the @ref iiopp::Context that created them. When a context gets destroyed,
 * all its child objects die as well. All wrapper classes have weak reference semantic and become invalid
 * when the context gets destroyed (or their referenced instance get destroyed by other means).
 *
 * In consequence all types are cheap to copy (at the cost of assigning a pointer). Copies refer
 * to the same underlying object.
 *
 * Types with <i>Ptr</i> at the end act like unique pointers: Their destructor destroys the object they refer to and
 * they are not copyable (but movable).
 *
 * See @ref iiopp-enum.cpp for an example.
 */
namespace iiopp
{

#if IIOPP_HAVE_STD_OPIONAL
using std::optional;
#else
using boost::optional;
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
    cstr(void const * s) : s(static_cast<char const *>(s)){assert(s);}

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

/** @brief Optional string, used for C-functions that return @c nullptr for "no value".
 */
typedef optional<cstr> optstr;


/**
@brief Namespace of implementation details
*/
namespace impl
{

inline optstr opt(char const * s)
{
    return s ? optstr{{s}} : optstr{};
}

template <class T, class C> optional<T> maybe(C * obj)
{
    return obj ? optional<T>{{obj}} : optional<T>{};
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
T * check(T * ret, char const * ctx)
{
    if (int e = iio_err(ret))
    {
        err(e, ctx);
    }

    return ret;
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
template <class container_T, class element_T> //, class size_T=std::size_t, class diff_T=std::ptrdiff_t>
class IndexedSequence
{
    container_T & _me() {return *static_cast<container_T*>(this);}
public:

    /** @brief A random access iterator for an @ref IndexedSequence
     */
    class Iterator
    {
    public:
        typedef std::random_access_iterator_tag iterator_category;
        typedef element_T value_type;
        typedef decltype(std::declval<container_T>().size()) size_type;
        typedef std::make_signed<size_type> difference_type;
        typedef element_T *pointer;
        typedef element_T &reference;

        Iterator(container_T &cont, size_type idx) : c(cont), i(idx) {assert(idx <= cont.size());}

        element_T operator*() const { return c[i]; }
        Iterator& operator++() { assert(i <= c.size()); ++i; return *this;}
        Iterator operator++(int) { Iterator tmp = *this; ++(*this); return tmp; }
        bool operator == (const Iterator& rhs) const { assert(&c == &rhs.c); return i == rhs.i; }
        bool operator != (const Iterator& rhs) const { assert(&c == &rhs.c); return i != rhs.i; }
        bool operator < (const Iterator& rhs) const { assert(&c == &rhs.c); return i < rhs.i; }
        bool operator > (const Iterator& rhs) const { assert(&c == &rhs.c); return i > rhs.i; }
        bool operator <= (const Iterator& rhs) const { assert(&c == &rhs.c); return i <= rhs.i; }
        bool operator >= (const Iterator& rhs) const { assert(&c == &rhs.c); return i >= rhs.i; }
        Iterator operator + (difference_type x) const { return Iterator(c, i + x); }
        int operator - (Iterator rhs) const { assert(&c == &rhs.c); return i - rhs.i; }
    private:
        container_T & c;
        size_type i;
    };

    Iterator begin() {return Iterator(_me(), 0);}
    Iterator end() {return Iterator(_me(), _me().size());}
};

}


/** @brief C++ wrapper for the @ref Attributes C-API
 */
class Attr
{
    iio_attr const * p;
public:
    Attr() = delete;
    Attr(iio_attr const * attr) : p(attr){assert(attr);}
    operator iio_attr const * () const {return p;}

    cstr name() {return iio_attr_get_name(p);}
    cstr filename() {return iio_attr_get_filename(p);}
    cstr static_value() {return iio_attr_get_static_value(p);}

    size_t read_raw(char * dst, size_t size) const {return impl::check_n(iio_attr_read_raw(p, dst, size), "iio_attr_read_raw");}
    bool read_bool() const {bool val; impl::check(iio_attr_read_bool(p, &val), "iio_attr_read_bool"); return val;}
    double read_double() const {double val; impl::check(iio_attr_read_double(p, &val), "iio_attr_read_double"); return val;}
    long long read_longlong() const {long long val; impl::check(iio_attr_read_longlong(p, &val), "iio_attr_read_longlong"); return val;}

    size_t write_raw(void const * src, size_t len) {return impl::check_n(iio_attr_write_raw(p, src, len), "iio_attr_write_raw");}
    size_t write_string(cstr val) {return impl::check_n(iio_attr_write_string(p, val), "iio_attr_write_string");}
    void write_bool(bool val) {impl::check(iio_attr_write_bool(p, val), "iio_attr_write_bool");}
    void write_double(double val) {impl::check(iio_attr_write_double(p, val), "iio_attr_write_double");}
    void write_longlong(long long val) {impl::check(iio_attr_write_longlong(p, val), "iio_attr_write_longlong");}

    operator bool () const {return read_bool();}
    operator double () const {return read_double();}
    operator long long () const {return read_longlong();}

    cstr operator = (cstr val){write_string(val); return val;}
    bool operator = (bool val){write_bool(val); return val;}
    double operator = (double val){write_double(val); return val;}
    long long operator = (long long val){write_longlong(val); return val;}
};

namespace impl
{

/** @brief Vector-like accessor for all attributes of an object
 */
#ifdef DOXYGEN
template <class obj_T>
class AttrSeqT : public IndexedSequence<AttrSeqT<obj_T>, Attr>
#else
template <class obj_T,
          unsigned int get_attrs_count_T(obj_T const *),
          iio_attr const * get_attr_T(obj_T const *, unsigned int),
          iio_attr const * find_attr_T(obj_T const *, char const *)
          >
class AttrSeqT : public IndexedSequence<AttrSeqT<obj_T, get_attrs_count_T, get_attr_T, find_attr_T>, Attr>
#endif
{
    obj_T const * const _obj;
public:
    AttrSeqT(obj_T const * obj) : _obj(obj){assert(obj);}

    /** @brief Count of attributes
     */
    unsigned int size() const {return get_attrs_count_T(_obj);}

    /** @brief Access by attribute index
     */
    Attr operator [](unsigned int idx)
    {
        if (auto ret = get_attr_T(_obj, idx))
            return Attr(ret);
        throw std::out_of_range("invalid attribute index");
    }

    /** @brief Access by attribute name
     */
    Attr operator [](cstr name)
    {
        if (auto ret = find_attr_T(_obj, name))
            return Attr(ret);
        throw std::out_of_range("invalid attribute name");
    }
};

template <class obj_T, iio_attr const * find_attr_T(obj_T const *, char const *)>
optional<Attr> attr(obj_T const * obj, cstr name)
{
    return maybe<Attr>(find_attr_T(obj, name));
}

template <class obj_T, iio_attr const * get_attr_T(obj_T const *, unsigned int)>
optional<Attr> attr(obj_T const * obj, unsigned int index)
{
    return maybe<Attr>(get_attr_T(obj, index));
}

} // namespace impl



/**
@brief Special unique pointer for instances that must be destroyed

@tparam obj_T Wrapper class
@tparam ptr_T Pointer type from the C-API
@tparam deleter_T Function that must be used for destroying objects
*/
template <class obj_T, class ptr_T, void deleter_T(ptr_T *)>
class Ptr
{
    obj_T p;
public:
    Ptr() = delete;
    Ptr(Ptr const &) = delete;
    Ptr(Ptr && rhs) : p(rhs.p) { rhs.p = nullptr;}
    explicit Ptr(ptr_T * obj) : p{obj}{}
    ~Ptr(){ if (p) deleter_T(const_cast<ptr_T*>(static_cast<ptr_T const*>(p)));}

    Ptr & operator = (Ptr &) = delete;
    void operator = (Ptr && rhs)
    {
        if (p)
            deleter_T(p);
        p = rhs.p;
        rhs.p = nullptr;
    }

    operator obj_T * () {return &p;}
    operator obj_T * () const {return &p;}
    obj_T * operator -> () {return &p;}
    obj_T const * operator -> () const {return &p;}
};

/** @brief C++ wrapper for @ref iio_channels_mask
 */
class ChannelsMask
{
    iio_channels_mask const * const p;
public:

    ChannelsMask() = delete;
    ChannelsMask(iio_channels_mask const * mask) : p(mask){assert(mask);}
    operator iio_channels_mask const * () const {return p;}
};

typedef Ptr<ChannelsMask, iio_channels_mask, iio_channels_mask_destroy> ChannelsMaskPtr;

ChannelsMaskPtr create_channels_mask(unsigned int nb_channels)
{
    return ChannelsMaskPtr(iio_create_channels_mask(nb_channels));
}

/** @brief C++ wrapper for the @ref Block C-API
 */
class Block
{
    iio_block * const p;
public:

    Block() = delete;
    Block(iio_block * block) : p(block){assert(block);}
    operator iio_block * () const {return p;}

    void * start() {return iio_block_start(p);}
    void * first(iio_channel * chn) {return iio_block_first(p, chn);}
    void * end() {return iio_block_end(p);}
    ssize_t foreach_sample(const struct iio_channels_mask *mask,
                             ssize_t (*callback)(const struct iio_channel *chn, void *src, size_t bytes, void *d),
                             void *data)
    {
        return iio_block_foreach_sample(p, mask, callback, data);
    }

    void enqueue(size_t bytes_used, bool cyclic) {impl::check(iio_block_enqueue(p, bytes_used, cyclic), "iio_block_enqueue");}
    void dequeue(bool nonblock) {impl::check(iio_block_dequeue(p, nonblock), "iio_block_dequeue");}
    Buffer buffer();
};

typedef Ptr<Block, iio_block, iio_block_destroy> BlockPtr;

/** @brief C++ wrapper for the @ref Channel C-API
 */
class Channel
{
    iio_channel * const p;
public:

    Channel() = delete;
    Channel(iio_channel * chan) : p(chan), attrs(chan){assert(chan);}
    operator iio_channel * () const {return p;}

#ifndef DOXYGEN
    typedef impl::AttrSeqT<iio_channel,
                           iio_channel_get_attrs_count,
                           iio_channel_get_attr,
                           iio_channel_find_attr
                           > AttrSeq;
#else
    typedef impl::AttrSeqT<Channel> AttrSeq;
#endif

    AttrSeq attrs;

    Device device() const;
    cstr id() const {return iio_channel_get_id(p);}
    optstr name() const { return impl::opt(iio_channel_get_name(p));}
    optstr label() const { return impl::opt(iio_channel_get_label(p));}
    bool is_output() const { return iio_channel_is_output(p);}
    bool is_scan_element() const { return iio_channel_is_scan_element(p);}
    unsigned int attrs_count() const {return iio_channel_get_attrs_count(p);}
    optional<Attr> attr(unsigned int index) {return impl::maybe<Attr>(iio_channel_get_attr(p, index));}
    optional<Attr> find_attr(cstr name) {return impl::maybe<Attr>(iio_channel_find_attr(p, name));}
    void enable(iio_channels_mask * mask) {iio_channel_enable(p, mask);}
    void disable(iio_channels_mask * mask) {iio_channel_disable(p, mask);}
    bool is_enabled(iio_channels_mask * mask) const { return iio_channel_is_enabled(p, mask);}
    size_t read(Block block, void * dst, size_t len, bool raw) const; // Flawfinder: ignore
    size_t write(Block block, void const * src, size_t len, bool raw);
    void set_data(void * data){iio_channel_set_data(p, data);}
    void * data() const {return iio_channel_get_data(p);}
    iio_chan_type type() const {return iio_channel_get_type(p);}
    iio_modifier modifier() const {return iio_channel_get_modifier(p);}
    hwmon_chan_type hwmon_type() const {return hwmon_channel_get_type(p);}
    unsigned long index() const { return impl::check_n(iio_channel_get_index(p), "iio_channel_get_index");}
    iio_data_format const * data_format() const {return iio_channel_get_data_format(p);}
    void convert(void * dst, void const * src) const {iio_channel_convert(p, dst, src);}
    void convert_inverse(void * dst, void const * src) const {iio_channel_convert_inverse(p, dst, src);}
};

/** @brief C++ wrapper for the @ref Stream C-API
 */
class Stream
{
    iio_stream * const p;
public:

    Stream() = delete;
    Stream(iio_stream * s) : p(s){assert(s);}
    operator iio_stream * () const {return p;}

    Block next_block() {return const_cast<iio_block *>(impl::check(iio_stream_get_next_block(p), "iio_stream_get_next_block")); }
};

typedef Ptr<Stream, iio_stream, iio_stream_destroy> StreamPtr;

/**
@brief Event object
*/
struct Event : public iio_event
{
    iio_event_type type() const { return iio_event_get_type(this);}
    iio_event_direction direction() const { return iio_event_get_direction(this);}
    optional<Channel> channel(iio_device * dev, bool diff){return impl::maybe<Channel>(const_cast<iio_channel*>(iio_event_get_channel(this, dev, diff)));}
};

/** @brief C++ wrapper for @ref iio_event_stream
 */
class EventStream
{
    iio_event_stream * const p;
public:

    EventStream() = delete;
    EventStream(iio_event_stream * s) : p(s){assert(p);}
    operator iio_event_stream * () const {return p;}

    Event read(bool nonblock) {iio_event ev; impl::check(iio_event_stream_read(p, &ev, nonblock), "iio_event_stream_read"); return static_cast<Event&>(ev);} // Flawfinder: ignore
};

typedef Ptr<EventStream, iio_event_stream, iio_event_stream_destroy> EventStreamPtr;


/** @brief C++ wrapper for the @ref Buffer C-API
 */
class Buffer
{
    iio_buffer * const p;
public:
    Buffer() = delete;
    Buffer(iio_buffer * buffer) : p(buffer), attrs(buffer){assert(buffer);}
    operator iio_buffer * () const {return p;}

#ifndef DOXYGEN
    typedef impl::AttrSeqT<iio_buffer,
                           iio_buffer_get_attrs_count,
                           iio_buffer_get_attr,
                           iio_buffer_find_attr
                           > AttrSeq;
#else
    typedef impl::AttrSeqT<Buffer> AttrSeq;
#endif

    AttrSeq attrs;

    Device device();
    unsigned int attrs_count() const {return iio_buffer_get_attrs_count(p);}
    optional<Attr> get_attr(unsigned int index) {return impl::maybe<Attr>(iio_buffer_get_attr(p, index));}
    optional<Attr> find_attr(cstr name) {return impl::maybe<Attr>(iio_buffer_find_attr(p, name));}
    void set_data(void * data){iio_buffer_set_data(p, data);}
    void * data() {return iio_buffer_get_data(p);}
    void cancel() {iio_buffer_cancel(p);}
    void enable() {impl::check(iio_buffer_enable(p), "iio_buffer_enable");}
    void disable() {impl::check(iio_buffer_disable(p), "iio_buffer_disable");}
    ChannelsMask channels_mask() {return iio_buffer_get_channels_mask(p);}
    BlockPtr create_block(size_t size) { return BlockPtr{impl::check(iio_buffer_create_block(p, size), "iio_buffer_create_block")}; }
    StreamPtr create_stream(size_t nb_blocks, size_t sample_count) { return StreamPtr{impl::check(iio_buffer_create_stream(p, nb_blocks, sample_count), "iio_buffer_create_stream")}; }
};

typedef Ptr<Buffer, iio_buffer, iio_buffer_destroy> BufferPtr;

/** @brief C++ wrapper for the @ref Device C-API
 */
class Device : public impl::IndexedSequence<Device, Channel>
{
    iio_device * const p;
public:

    unsigned int size() const
    {
        return channels_count();
    }

    Channel operator [](unsigned int i)
    {
        if (auto maybeCh = channel(i))
            return *maybeCh;

        throw std::out_of_range("channel index out of range");
    }

#ifndef DOXYGEN
    typedef impl::AttrSeqT<iio_device,
        iio_device_get_attrs_count,
        iio_device_get_attr,
        iio_device_find_attr
        > AttrSeq;
#else
    typedef impl::AttrSeqT<Device> AttrSeq;
#endif

    AttrSeq attrs;

#ifndef DOXYGEN
    typedef impl::AttrSeqT<iio_device,
        iio_device_get_debug_attrs_count,
        iio_device_get_debug_attr,
        iio_device_find_debug_attr
        > DebugAttrSeq;
#else
    typedef impl::AttrSeqT<Device> DebugAttrSeq;
#endif

    DebugAttrSeq debug_attrs;


    Device() = delete;
    Device(iio_device * dev) : p(dev), attrs(dev), debug_attrs(dev) {assert(dev);}
    operator iio_device * () const {return p;}

    Context context();
    cstr id() const { return iio_device_get_id(p);}
    optstr name() const { return impl::opt(iio_device_get_name(p));}
    optstr label() const { return impl::opt(iio_device_get_label(p));}
    unsigned int channels_count() const {return iio_device_get_channels_count(p);}
    unsigned int attrs_count() const {return iio_device_get_attrs_count(p);}
    optional<Channel> channel(unsigned int idx) const {return impl::maybe<Channel>(iio_device_get_channel(p, idx));}
    optional<Attr> attr(unsigned int idx) {return impl::attr<iio_device, iio_device_get_attr>(p, idx);}
    optional<Channel> find_channel(cstr name, bool output) const {return impl::maybe<Channel>(iio_device_find_channel(p, name, output));}
    optional<Attr> find_attr(cstr name) {return impl::attr<iio_device, iio_device_find_attr>(p, name);}
    void set_data(void * data){iio_device_set_data(p, data);}
    void * data() const {return iio_device_get_data(p);}
    Device trigger() const {return Device{const_cast<iio_device*>(impl::check(iio_device_get_trigger(p), "iio_device_get_trigger"))};}
    void set_trigger(iio_device const * trigger) {impl::check(iio_device_set_trigger(p, trigger), "iio_device_set_trigger");}
    bool is_trigger() const {return iio_device_is_trigger(p);}
    BufferPtr create_buffer(unsigned int idx, iio_channels_mask * mask) {return BufferPtr(impl::check(iio_device_create_buffer(p, idx, mask), "iio_device_create_buffer"));}
    bool is_hwmon() const {return iio_device_is_hwmon(p);}
    EventStreamPtr create_event_stream() { return EventStreamPtr{impl::check(iio_device_create_event_stream(p), "iio_device_create_event_stream")};}
    ssize_t sample_size(iio_channels_mask * mask) const {return impl::check_n(iio_device_get_sample_size(p, mask), "iio_device_get_sample_size");}
    unsigned int debug_attrs_count() const {return iio_device_get_debug_attrs_count(p);}
    optional<Attr> debug_attr(unsigned int idx) {return impl::attr<iio_device, iio_device_get_debug_attr>(p, idx);}
    optional<Attr> find_debug_attr(cstr name) {return impl::attr<iio_device, iio_device_find_debug_attr>(p, name);}
    void reg_write(uint32_t address, uint32_t value) {impl::check(iio_device_reg_write(p, address, value), "iio_device_reg_write");}
    uint32_t reg_read(uint32_t address) {uint32_t value; impl::check(iio_device_reg_read(p, address, &value), "iio_device_reg_read"); return value;}
};

typedef Ptr<cstr, void, free> CstrPtr;

/** @brief C++ wrapper for the @ref Context C-API
 */
class Context : public impl::IndexedSequence<Context, Device>
{
    iio_context * const p;
public:
    unsigned int size() const
    {
        return devices_count();
    }

    Device operator [](unsigned int i)
    {
        if (auto maybeDev = device(i))
            return *maybeDev;

        throw std::out_of_range("device index out of range");
    }

#ifndef DOXYGEN
    typedef impl::AttrSeqT<iio_context,
                           iio_context_get_attrs_count,
                           iio_context_get_attr,
                           iio_context_find_attr
                           > AttrSeq;
#else
    typedef impl::AttrSeqT<Context> AttrSeq;
#endif

    AttrSeq attrs;


    Context() = delete;
    Context(iio_context * ctx) : p(ctx), attrs(ctx) {assert(ctx);}
    operator iio_context * () const {return p;}

    unsigned int version_major() const { return iio_context_get_version_major(p); }
    unsigned int version_minor() const { return iio_context_get_version_minor(p); }
    cstr version_tag() const { return iio_context_get_version_tag(p); }
    CstrPtr xml() const { return CstrPtr{impl::check(iio_context_get_xml(p), "iio_context_get_xml")};}
    cstr name() const { return iio_context_get_name(p); }
    cstr description() const { return iio_context_get_description(p); }
    unsigned int attrs_count() const {return iio_context_get_attrs_count(p);}
    optional<Attr> attr(unsigned int idx) {return impl::attr<iio_context, iio_context_get_attr>(p, idx);}
    optional<Attr> find_attr(cstr name) {return impl::attr<iio_context, iio_context_find_attr>(p, name);}
    unsigned int devices_count() const {return iio_context_get_devices_count(p);}
    optional<Device> device(unsigned int idx) const { return impl::maybe<Device>(iio_context_get_device(p, idx)); }
    optional<Device> find_device(cstr name) const {return impl::maybe<Device>(iio_context_find_device(p, name));}
    void set_timeout(unsigned int timeout_ms){impl::check(iio_context_set_timeout(p, timeout_ms), "iio_context_set_timeout");}
    iio_context_params const * params() const {return iio_context_get_params(p);}
    void set_data(void * data){iio_context_set_data(p, data);}
    void * data() const {return iio_context_get_data(p);}
};

typedef Ptr<Context, iio_context, iio_context_destroy> ContextPtr;

inline Buffer Block::buffer() {return iio_block_get_buffer(p);}
inline Context Device::context(){return const_cast<iio_context*>(iio_device_get_context(p));}
inline Device Channel::device() const {return const_cast<iio_device*>(iio_channel_get_device(p));}
inline Device Buffer::device()  {return const_cast<iio_device*>(iio_buffer_get_device(p));}
inline size_t Channel::read(Block block, void * dst, size_t len, bool raw) const {return iio_channel_read(p, block, dst, len, raw);} // Flawfinder: ignore
inline size_t Channel::write(Block block, void const * src, size_t len, bool raw) {return iio_channel_write(p, block, src, len, raw);}

/** @brief C++ wrapper for @ref iio_create_context
 */
inline ContextPtr create_context(iio_context_params * params, const char * uri)
{
    return ContextPtr{impl::check(iio_create_context(params, uri), "iio_create_context")};
}

class Scan;

class ScanResult
{
    struct iio_scan const * const p;
    size_t const idx;

    friend class Scan;

    ScanResult(struct iio_scan const * scan, size_t index) : p(scan), idx(index){assert(scan);}
public:
    cstr description() const { return iio_scan_get_description(p, idx);}
    cstr uri() const { return iio_scan_get_description(p, idx);}
};

/** @brief C++ wrapper for the @ref Scan C-API
 */
class Scan : public impl::IndexedSequence<Scan, ScanResult>
{
    struct iio_scan const * const p;
public:
    size_t size() const
    {
        return results_count();
    }

    ScanResult operator [](size_t i)
    {
        assert(i < results_count());
        return ScanResult{p, i};
    }

    Scan() = delete;
    Scan(struct iio_scan const * scan) : p(scan){assert(scan);}
    operator struct iio_scan const * () const {return p;}
    size_t results_count() const { return iio_scan_get_results_count(p);}
};

typedef Ptr<Scan, struct iio_scan, iio_scan_destroy> ScanPtr;

ScanPtr scan(struct iio_context_params const * params, char const * backends)
{
    return ScanPtr(impl::check(iio_scan(params, backends), "iio_scan"));
}


/** @brief Reads the value of a channel by using "input" or "raw" attribute and applying "scale" and "offset" if available
 *
 * @see @c get_channel_value in the example @ref iio-monitor.c
 */
inline double value(Channel ch)
{
    if (auto att = ch.find_attr("input"))
        return att->read_double() / 1000;

    double scale = 1;
    if (auto att = ch.find_attr("scale"))
        scale = att->read_double();

    double offset = 0;
    if (auto att = ch.find_attr("offset"))
        offset = att->read_double();

    if (auto att = ch.find_attr("raw"))
        return (att->read_double() + offset) * scale / 1000.;

    impl::err(ENOENT, "channel does not provide raw value");
}

} // namespace iiopp
