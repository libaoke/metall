// Copyright 2019 Lawrence Livermore National Security, LLC and other Metall Project Developers.
// See the top-level COPYRIGHT file for details.
//
// SPDX-License-Identifier: (Apache-2.0 OR MIT)

#ifndef METALL_V0_KERNEL_MANAGER_KERNEL_HPP
#define METALL_V0_KERNEL_MANAGER_KERNEL_HPP

#include <iostream>
#include <cassert>
#include <string>
#include <utility>
#include <memory>
#include <future>
#include <vector>
#include <map>

#include <metall/offset_ptr.hpp>
#include <metall/v0/kernel/manager_kernel_fwd.hpp>
#include <metall/v0/kernel/bin_number_manager.hpp>
#include <metall/v0/kernel/bin_directory.hpp>
#include <metall/v0/kernel/chunk_directory.hpp>
#include <metall/v0/kernel/named_object_directory.hpp>
#include <metall/v0/kernel/segment_storage.hpp>
#include <metall/v0/kernel/segment_header.hpp>
#include <metall/v0/kernel/object_size_manager.hpp>
#include <metall/detail/utility/common.hpp>
#include <metall/detail/utility/in_place_interface.hpp>
#include <metall/detail/utility/array_construct.hpp>
#include <metall/detail/utility/mmap.hpp>
#include <metall/detail/utility/file.hpp>
#include <metall/detail/utility/char_ptr_holder.hpp>
#include <metall/detail/utility/soft_dirty_page.hpp>
#include <metall/v0/kernel/allocator_kernel.hpp>

#define ENABLE_MUTEX_IN_V0_MANAGER_KERNEL 1
#if ENABLE_MUTEX_IN_V0_MANAGER_KERNEL
#include <metall/detail/utility/mutex.hpp>
#endif

namespace metall {
namespace v0 {
namespace kernel {

namespace {
namespace util = metall::detail::utility;
}

template <typename _chunk_no_type, std::size_t _chunk_size, typename _internal_data_allocator_type>
class manager_kernel {

 public:
  // -------------------------------------------------------------------------------- //
  // Public types and static values
  // -------------------------------------------------------------------------------- //
  using void_pointer = offset_ptr<void>;
  using char_type = char; // required by boost's named proxy
  using char_ptr_holder_type = util::char_ptr_holder<char_type>;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using id_type = uint16_t;

  using chunk_no_type = _chunk_no_type;
  static constexpr std::size_t k_chunk_size = _chunk_size;
  using internal_data_allocator_type = _internal_data_allocator_type;

 private:
  // -------------------------------------------------------------------------------- //
  // Private types and static values
  // -------------------------------------------------------------------------------- //
  using self_type = manager_kernel<_chunk_no_type, _chunk_size, _internal_data_allocator_type>;

  static constexpr size_type k_max_size = 1ULL << 48ULL; // TODO: get from somewhere else

  static constexpr const char *k_segment_file_name = "segment";

  // For named object directory
  using named_object_directory_type = named_object_directory<difference_type, size_type, internal_data_allocator_type>;
  static constexpr const char *k_named_object_directory_file_name = "named_object_directory";

  // For data segment manager
  using segment_header_type = segment_header<k_chunk_size>;
  using segment_storage_type = segment_storage<difference_type, size_type, sizeof(segment_header_type)>;

  // For actual memory allocatation
  using segment_memory_allocator = allocator_kernel<chunk_no_type, size_type, difference_type,
                                                    k_chunk_size, k_max_size,
                                                    internal_data_allocator_type>;

  // For snapshot
  static constexpr size_type k_snapshot_no_safeguard = 1ULL << 20ULL; // Safeguard, you could increase this number
  static constexpr const char *k_diff_snapshot_file_name_prefix = "ssdf";
  static constexpr size_type k_min_snapshot_no = 1;

#if ENABLE_MUTEX_IN_V0_MANAGER_KERNEL
  using mutex_type = util::mutex;
  using lock_guard_type = util::mutex_lock_guard;
#endif

 public:
  // -------------------------------------------------------------------------------- //
  // Constructor & assign operator
  // -------------------------------------------------------------------------------- //
  explicit manager_kernel(const internal_data_allocator_type &allocator);
  ~manager_kernel();

  manager_kernel(const manager_kernel &) = delete;
  manager_kernel &operator=(const manager_kernel &) = delete;

  manager_kernel(manager_kernel &&) = default;
  manager_kernel &operator=(manager_kernel &&) = default;

 public:
  // -------------------------------------------------------------------------------- //
  // Public methods
  // -------------------------------------------------------------------------------- //
  /// \brief Expect to be called by a single thread
  /// \param path
  /// \param nbytes
  void create(const char *path, size_type nbytes);

  /// \brief Expect to be called by a single thread
  /// \param path
  /// \return
  bool open(const char *path);

  /// \brief Expect to be called by a single thread
  void close();

  /// \brief Sync with backing files
  /// \param sync If true, performs synchronous synchronization;
  /// otherwise, performs asynchronous synchronization
  void sync(bool sync);

  /// \brief Allocates memory space
  /// \param nbytes
  /// \return
  void *allocate(size_type nbytes);

  // \TODO: implement
  void *allocate_aligned(size_type nbytes, size_type alignment);

  /// \brief Deallocates
  /// \param addr
  void deallocate(void *addr);

  /// \brief Finds an already constructed object
  /// \tparam T
  /// \param name
  /// \return
  template <typename T>
  std::pair<T *, size_type> find(char_ptr_holder_type name);

  /// \brief Destroy an already constructed object
  /// \tparam T
  /// \param name
  /// \return
  template <typename T>
  bool destroy(char_ptr_holder_type name);

  /// \brief Generic named/anonymous new function. This method is required by construct_proxy and construct_iter_proxy
  /// \tparam T Type of the object(s)
  /// \param name Name of the object(s)
  /// \param num Number of objects to be constructed
  /// \param try2find If true, tries to find already constructed object(s) with the same name
  /// \param dothrow If true, throws exception
  /// \param table Reference to an in_place_interface object
  /// \return Returns a pointer to the constructed object(s)
  template <typename T>
  T *generic_construct(char_ptr_holder_type name,
                       size_type num,
                       bool try2find,
                       bool dothrow,
                       util::in_place_interface &table);

  /// \brief Get the address of the segment header
  /// \return Returns the adress of the segment header
  segment_header_type *get_segment_header() const;

  /// \brief
  /// \param base_path
  /// \return
  bool snapshot(const char *destination_base_path);

  /// \brief
  /// \param destination_base_path
  /// \return
  bool snapshot_diff(const char *destination_base_path);

  /// \brief Copies backing files synchronously
  /// \param source_base_path
  /// \param destination_base_path
  /// \return If succeeded, returns True; other false
  static bool copy_file(const char *source_base_path, const char *destination_base_path);

  /// \brief Copies backing files asynchronously
  /// \param source_base_path
  /// \param destination_base_path
  /// \return Returns an object of std::future
  /// If succeeded, its get() returns True; other false
  static std::future<bool> copy_file_async(const char *source_base_path, const char *destination_base_path);

  /// \brief Remove backing files synchronously
  /// \param base_path
  /// \return If succeeded, returns True; other false
  static bool remove_file(const char *base_path);

  /// \brief Remove backing files asynchronously
  /// \param base_path
  /// \return Returns an object of std::future
  /// If succeeded, its get() returns True; other false
  static std::future<bool> remove_file_async(const char *base_path);

  /// \brief Show some profile infromation
  /// \tparam out_stream_type
  /// \param log_out
  template <typename out_stream_type>
  void profile(out_stream_type *log_out) const;

 private:
  // -------------------------------------------------------------------------------- //
  // Private types and static values
  // -------------------------------------------------------------------------------- //

  // -------------------------------------------------------------------------------- //
  // Private methods (not designed to be used by the base class)
  // -------------------------------------------------------------------------------- //
  static std::string priv_make_file_name(const std::string &base_name, const std::string &item_name);

  bool priv_initialized() const;

  std::string priv_make_file_name(const std::string &item_name) const;

  size_type priv_num_chunks() const;

  void priv_init_segment_header();

  template <typename T>
  T *priv_generic_named_construct(const char_type *name,
                                  size_type num,
                                  bool try2find,
                                  bool, // TODO implement 'dothrow'
                                  util::in_place_interface &table);

  // ---------------------------------------- For serializing/deserializing ---------------------------------------- //
  bool priv_serialize_management_data();

  bool priv_deserialize_management_data(const char *base_path);

  // ---------------------------------------- File operations ---------------------------------------- //
  static bool priv_copy_backing_file(const char *src_base_name,
                                     const char *dst_base_name,
                                     const char *item_name);

  /// \brief Copies all backing files
  static bool priv_copy_all_backing_files(const char *src_base_name,
                                          const char *dst_base_name);

  /// \brief Copies all backing files including snapshots
  static bool priv_copy_all_backing_files(const char *src_base_name,
                                          const char *dst_base_name,
                                          size_type max_snapshot_no);

  static bool priv_remove_backing_file(const char *base_name, const char *item_name);

  /// \brief Removes all backing files
  static bool priv_remove_all_backing_files(const char *base_name);

  /// \brief Removes all backing files including snapshots
  static bool priv_remove_all_backing_files(const char *base_name,
                                            size_type max_snapshot_no);

  // ---------------------------------------- Snapshot ---------------------------------------- //
  bool priv_snapshot_entire_data(const char *snapshot_base_path) const;

  bool priv_snapshot_diff_data(const char *snapshot_base_path) const;

  static std::string priv_make_snapshot_base_file_name(const std::string &base_name,
                                                       size_type snapshot_no);

  static std::vector<std::string> priv_make_all_snapshot_base_file_names(const std::string &base_name,
                                                                         size_type max_snapshot_no);

  static bool reset_soft_dirty_bit();

  /// \brief Assume the minimum snapshot number is 1
  /// \param snapshot_base_path
  /// \return Returns 0 on error
  static size_type priv_find_next_snapshot_no(const char *snapshot_base_path);

  /// \brief Format of a diff file:
  /// Line 1: #of diff pages (N) in the file
  /// Line [2 ~ N+1): list of diff page numbers
  /// Line [N+1 + 2N +1): list of diffs by page
  bool priv_snapshot_segment_diff(const char *snapshot_base_file_name) const;

  auto priv_get_soft_dirty_page_no_list() const;

  auto priv_merge_segment_diff_list(const char *snapshot_base_path,
                                    size_type max_snapshot_no) const;

  bool priv_apply_segment_diff(const char *snapshot_base_path,
                               size_type max_snapshot_no,
                               const std::map<size_type, std::pair<size_type, size_type>> &segment_diff_list);

  // -------------------------------------------------------------------------------- //
  // Private fields
  // -------------------------------------------------------------------------------- //
  std::string m_file_base_path;
  named_object_directory_type m_named_object_directory;
  segment_storage_type m_segment_storage;
  segment_memory_allocator m_segment_memory_allocator;

#if ENABLE_MUTEX_IN_V0_MANAGER_KERNEL
  mutex_type m_chunk_mutex;
  mutex_type m_named_object_directory_mutex;
#endif
};

} // namespace kernel
} // namespace v0
} // namespace metall

#endif //METALL_V0_KERNEL_MANAGER_KERNEL_HPP

#include <metall/v0/kernel/manager_kernel_impl.ipp>
#include <metall/v0/kernel/manager_kernel_profile_impl.ipp>