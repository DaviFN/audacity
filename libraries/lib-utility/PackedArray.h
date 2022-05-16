/*!********************************************************************

 Audacity: A Digital Audio Editor

 @file PackedArray.h

 @brief Smart pointer for a header contiguous with an array holding a
 dynamically determined number of elements

 Paul Licameli

 **********************************************************************/

#ifndef __AUDACITY_PACKED_ARRAY__
#define __AUDACITY_PACKED_ARRAY__

#include <type_traits>

namespace PackedArray {

//! Primary template used in Deleter that can be specialized
template<typename T> struct Traits{
   // Default assumption is that there is no header and no need to overlay
   // the element with a type that performs nontrivial destruction
   struct header_type {};
   using element_type = T;
   using iterated_type = T;
};

//! Deleter for a header structure contiguous with an array of elements
/*!
 @tparam Type is the type pointed to, and an explicit specialization of
 Traits<Type> may redefine the nested types for a non-empty header:
  - header_type, which overlays the members of Type except the last member;
   may be non-empty and define a destructor
  - element_type must be such that the last member of Type is Element[1]; may
    define a destructor
 @tparam BaseDeleter manages the main deallocation
 */
template<typename Type,
   template<typename> typename BaseDeleter = std::default_delete>
struct Deleter : BaseDeleter<Type> {
   using managed_type = Type;
   using base_type = BaseDeleter<managed_type>;
   using traits_type = Traits<managed_type>;
   using header_type = typename traits_type::header_type;
   using element_type = typename traits_type::element_type;

   // Check our assumptions about alignments
   // Imitation of the layout of the managed type.  Be sure to use
   // empty base class optimization in case header_type is zero-sized.
   struct Overlay : header_type { element_type elements[1]; };
   static_assert(sizeof(Overlay) == sizeof(managed_type));
   // Another sanity check.  Note sizeof() an empty type is nonzero
   static_assert(offsetof(Overlay, elements) ==
      std::is_empty_v<header_type> ? 0 : sizeof(header_type));

   //! Nontrivial, implicit constructor of the deleter takes a size, which
   //! defaults to 0 to allow default contruction of the unique_ptr
   Deleter(size_t size = 0) noexcept
      : mCount{ (size - sizeof(header_type)) / sizeof(element_type) } {}

   void operator()(Type *p) const noexcept(noexcept(
      (void)std::declval<element_type>().~element_type(),
      (void)std::declval<header_type>().~header_type(),
      (void)std::declval<base_type>()(p)
   )) {
      if (!p)
         return;
      auto pOverlay = reinterpret_cast<Overlay*>(p);
      auto pE = pOverlay->elements + mCount;
      for (auto count = mCount; count--;)
         // Do nested deallocations for elements by decreasing subscript
         (--pE)->~element_type();
      // Do nested deallocations for main structure
      pOverlay->~header_type();
      // main deallocation
      ((base_type&)*this)(p);
   }

   size_t GetCount() const { return mCount; }
private:
   size_t mCount = 0;
};

//! Smart pointer type that deallocates with Deleter
template<typename Type,
   template<typename> typename BaseDeleter = std::default_delete>
struct Ptr
   : std::unique_ptr<Type, Deleter<Type, BaseDeleter>>
{
   using
      std::unique_ptr<Type, Deleter<Type, BaseDeleter>>::unique_ptr;

   //! Enables subscripting, if Traits<Type>::iterated_type is
   //! defined.  Does not check for null!
   auto &operator[](size_t ii) const
   {
      return *(begin(*this) + ii);
   }
};

//! Find out how many elements were allocated with a Ptr
template<typename Type, template<typename> typename BaseDeleter>
inline size_t Count(const Ptr<Type, BaseDeleter> &p)
{
   return p.get_deleter().GetCount();
}

//! Enables range-for, if Traits<Type>::iterated_type is defined
template<typename Type, template<typename> typename BaseDeleter>
inline auto begin(const Ptr<Type, BaseDeleter> &p)
{
   void *ptr = p.get();
   using header_type = typename Traits<Type>::header_type;
   return reinterpret_cast<typename Traits<Type>::iterated_type *>(
      ptr
         ? static_cast<char*>(ptr) +
            (std::is_empty_v<header_type> ? 0 : sizeof(header_type))
         : nullptr
   );
}

//! Enables range-for, if Traits<Type>::iterated_type is defined
template<typename Type, template<typename> typename BaseDeleter>
inline auto end(const Ptr<Type, BaseDeleter> &p)
{
   auto result = begin(p);
   if (result)
      result += Count(p);
   return result;
}

}

struct PackedArray_t{};
//! Tag argument to distinguish an overload of ::operator new
static constexpr inline PackedArray_t PackedArrayArg;

//! Allocator for objects managed by PackedArray::Ptr enlarges the size request
//! that the compiler makes
inline void *operator new(size_t size, PackedArray_t, size_t enlarged) {
   return ::operator new(std::max(size, enlarged));
}

//! Complementary operator delete in case of exceptions in a new-expression
inline void operator delete(void *p, PackedArray_t, size_t) {
   ::operator delete(p);
}

namespace PackedArray {

//! Allocate a Ptr<Type> holding at least `enlarged` bytes
/*!
 Usage: AllocateBytes<Type>(bytes)(constructor arguments for Type)
 Meant to resemble placement-new syntax with separation of allocator and
 constructor arguments
 */
template<typename Type> auto AllocateBytes(size_t enlarged)
{
   return [enlarged](auto &&...args) {
      return Ptr<Type>{
         safenew(PackedArrayArg, enlarged)
            Type{ std::forward<decltype(args)>(args)... },
         std::max(sizeof(Type), enlarged) // Deleter's constructor argument
      };
   };
}

//! Allocate a Ptr<Type> holding `count` elements
/*!
 Usage: AllocateCount<Type>(count)(constructor arguments for Type)
 Meant to resemble placement-new syntax with separation of allocator and
 constructor arguments

 If Traits<Type> defines a nonempty header, the result always holds
 at least one element
 */
template<typename Type> auto AllocateCount(size_t count)
{
   using header_type = typename Traits<Type>::header_type;
   size_t bytes = (std::is_empty_v<header_type> ? 0 : sizeof(header_type))
      + count * sizeof(typename Traits<Type>::element_type);
   return AllocateBytes<Type>(bytes);
}

}

#endif
