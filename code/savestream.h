/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "abstract.h"
#include "loco.h"
#include "serialize.h"
#include "swizzle.h"
#include "win.h"

#include <array>
#include <cstddef>
#include <deque>
#include <new>
#include <optional>
#include <source_location>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <unordered_map>
#include <vector>

class SaveStreamClass;

/*
 * A pointer is worth swizzling only when the object it names announces a swizzle
 * identity as it loads. The two persistent roots do that; nothing else does.
 */
template<typename T>
concept SwizzleTarget = std::is_base_of_v<AbstractClass, std::remove_cv_t<T>>
	|| std::is_base_of_v<LocomotionClass, std::remove_cv_t<T>>;

template<typename T>
concept HasSerializeMember = requires(T & object, SaveStreamClass & stream) {
	object.Serialize(stream);
};

/*
 * A container takes the caller's source location, so that an unresolved pointer names
 * the member it belongs to rather than the loop inside the container.
 */
template<typename T>
concept HasSerializeMemberWhere = requires(T & object, SaveStreamClass & stream, std::source_location const & where) {
	object.Serialize(stream, where);
};

/*
 * A class that lays its contents out in order rather than as named members says so, and
 * travels as one field rather than as a body of its own.
 */
template<typename T>
concept SerializesPositionally = requires { typename T::SerializePositional; };


/*
 * This carries one object's members to and from a save game. The same Serialize call
 * reads or writes depending on the mode the stream was opened in, so a class describes
 * its members once and cannot drift between saving and loading.
 *
 * Two rules govern what may be handed to Serialize:
 *
 * A pointer must be serialized where it finally lives. The swizzle manager remembers
 * the address of the slot it has to patch, so a pointer routed through a temporary, or
 * held in storage that is reallocated afterwards, is fixed up over memory that no
 * longer belongs to the object.
 *
 * A union may travel as its raw image only while every alternative is trivially
 * copyable and free of pointers. One that gains a pointer has to be serialized arm by
 * arm, switched on whatever discriminates it.
 */
/*
 * The names every member and section of one save file is known by, shared by every stream
 * of that file since a name means the same thing throughout it.
 */
class SaveNamesClass
{
	public:
		/*
		 * What the table records a field's payload as: its width in bytes, which is 1, 2,
		 * 4 or 8, or KIND_VARIABLE for a payload that begins with its own length. A width
		 * is what lets a reader step over a field it has no member for.
		 */
		static unsigned char const KIND_VARIABLE = 0;

		// A name is at most this long and a file carries at most this many, so a damaged
		// table is refused rather than sized from.
		static std::size_t const MAX_NAMES = 65536;
		static std::size_t const MAX_NAME_LENGTH = 255;

		struct NameEntry
		{
			std::string Name;
			unsigned char Kind;
		};

		void Clear(void);

		// The identifier a name and width are known by, minting one the first time the
		// name is met. Answers false when the table is full.
		bool Intern(char const * name, unsigned char kind, unsigned short & id);

		// The identifier a name and width already have, for a load.
		bool Find(char const * name, unsigned char kind, unsigned short & id) const;

		std::size_t Count(void) const {return(Names.size());}
		unsigned char Kind_Of(unsigned short id) const {return(Names[id].Kind);}
		bool Is_Known(unsigned short id) const {return((std::size_t)id < Names.size());}

		void Write(std::vector<unsigned char> & out) const;
		bool Read(unsigned char const * data, std::size_t length);

		// How many bytes the table occupies when written.
		std::size_t Byte_Size(void) const;

	private:
		std::vector<NameEntry> Names;
		std::unordered_map<std::string, unsigned short> Identifiers;
};


class SaveStreamClass
{
	public:
		static unsigned char const KIND_VARIABLE = SaveNamesClass::KIND_VARIABLE;

		enum ModeType {
			MODE_SAVE,
			MODE_LOAD
		};

		SaveStreamClass(std::vector<unsigned char> & buffer, ModeType mode, SaveNamesClass & names,
			unsigned int start = 0);

		bool Is_Saving(void) const {return(Mode == MODE_SAVE);}
		bool Is_Loading(void) const {return(Mode == MODE_LOAD);}

		/*
		 * The first stream failure freezes this object and every Serialize that follows
		 * does nothing, so a class lists its members without checking each one and the
		 * caller asks once whether the whole pass worked.
		 */
		bool Was_Error(void) const {return(Failed);}

		/*
		 * Stops the pass here. A container that reads back a length no honest save could
		 * have written calls this rather than sizing itself from it.
		 */
		void Fail(void);

		/*
		 * The version stamp of the save being read, or the one being written. A member
		 * added to a later format is serialized only when this reaches the version that
		 * introduced it.
		 */
		unsigned int Version(void) const {return(FormatVersion);}

		/*
		 * Names the record this stream is carrying, so that a pointer which nothing
		 * answers for can be reported against the object that asked for it.
		 */
		void Set_Context(char const * ownertype, SwizzleIDType ownerid = 0)
		{
			OwnerType = ownertype;
			OwnerID = ownerid;
		}
		char const * Context_Type(void) const {return(OwnerType);}
		SwizzleIDType Context_ID(void) const {return(OwnerID);}

		/*
		 * Where the next byte goes or comes from, so a record can be framed by its length.
		 */
		unsigned int Offset(void) const {return(Cursor);}

		/*
		 * Holds a load to one record while it is read, so that a member reading more than
		 * its record holds is refused rather than spending the bytes of the record after
		 * it. A record nested in another leaves the outer one bounded as it was.
		 */
		class BoundScope
		{
			public:
				BoundScope(SaveStreamClass & stream, unsigned int end)
					: Stream(stream), Previous(stream.Limit)
				{
					if (end <= Stream.Limit) {
						Stream.Limit = end;
					}
				}

				~BoundScope(void) {Stream.Limit = Previous;}

				BoundScope(BoundScope const &) = delete;
				BoundScope & operator=(BoundScope const &) = delete;

			private:
				SaveStreamClass & Stream;
				unsigned int Previous;
		};
		unsigned int Size(void) const {return((unsigned int)Buffer->size());}
		void Overwrite_Bytes(unsigned int offset, void const * data, int length);

		void Serialize_Bytes(void * data, int length);

		/*
		 * Use the SERIALIZE macro rather than a name of one's own: the name is the member's
		 * identity in the file. A member the file does not name keeps what its owner built
		 * it with.
		 */
		template<typename T>
		void Serialize(char const * name, T & value, std::source_location const & where = std::source_location::current())
		{
			if constexpr (std::is_pointer_v<T>) {
				Fixed_Field(name, (unsigned char)sizeof(SwizzleIDType), [&]{ Serialize_Raw(value, where); });
			} else if constexpr ((std::is_arithmetic_v<T> || std::is_enum_v<T>)
					&& (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8)) {
				Fixed_Field(name, (unsigned char)sizeof(T), [&]{ Serialize_Raw(value); });
			} else if constexpr ((HasSerializeMember<T> || HasSerializeMemberWhere<T>) && !SerializesPositionally<T>) {
				// An object is a body of its own, so its members are named inside it and
				// the body's length is what the field is measured by.
				Body_Field(name, [&]{
					if constexpr (requires { Serialize_Raw(value, where); }) {
						Serialize_Raw(value, where);
					} else {
						Serialize_Raw(value);
					}
				});
			} else {
				Field(name, [&]{
					if constexpr (requires { Serialize_Raw(value, where); }) {
						Serialize_Raw(value, where);
					} else {
						Serialize_Raw(value);
					}
				});
			}
		}

		// A member whose interior the class lays out itself: a hand-rolled container, or a
		// flag and the value it guards.
		template<typename F>
		void Field(char const * name, F && interior)
		{
			unsigned int mark = 0;
			if (!Open_Field(name, KIND_VARIABLE, mark)) {
				return;
			}
			interior();
			Close_Field(mark, true);
		}

		// A width the table can record, so that a reader which does not know the name can
		// step over the field anyway.
		template<typename F>
		void Fixed_Field(char const * name, unsigned char width, F && interior)
		{
			unsigned int mark = 0;
			if (!Open_Field(name, width, mark)) {
				return;
			}
			interior();
			Close_Field(mark, false);
		}

		// A field measured by the body inside it, so the length is not written twice.
		template<typename F>
		void Body_Field(char const * name, F && interior)
		{
			unsigned int mark = 0;
			if (!Open_Field(name, KIND_VARIABLE, mark, true)) {
				return;
			}
			interior();
			Close_Field(mark, false);
		}

		// A base and the class built on it may name two members alike, and either may gain
		// one, because the base keeps a body of its own.
		template<typename F>
		void Base(char const * name, F && interior)
		{
			Body_Field(name, [&]{
				Begin_Body();
				interior();
				End_Body();
			});
		}

		// A reader steps over a body by its length whether or not it knows the class.
		void Begin_Body(void);
		void End_Body(void);

		// The same framing for a run with no names in it, so nothing inside is indexed.
		void Begin_Block(void);
		void End_Block(void);

		template<typename F>
		auto Block(F && interior) -> decltype(interior())
		{
			if constexpr (std::is_void_v<decltype(interior())>) {
				Begin_Block();
				interior();
				End_Block();
			} else {
				Begin_Block();
				auto const answer = interior();
				End_Block();
				return(answer);
			}
		}

		// For a class whose named members are reached through a Save or Load of its own
		// rather than through this stream.
		template<typename F>
		auto Body(F && interior) -> decltype(interior())
		{
			if constexpr (std::is_void_v<decltype(interior())>) {
				Begin_Body();
				interior();
				End_Body();
			} else {
				Begin_Body();
				auto const answer = interior();
				End_Body();
				return(answer);
			}
		}



		/*
		 * Refuses a count that the bytes left in the stream could not hold, so a damaged
		 * count fails the load before anything is allocated for it. Nothing serializes
		 * an element in less than a byte.
		 */
		bool Fits(int count, std::size_t each)
		{
			if (Is_Loading()) {
				std::size_t const room = (std::size_t)(Limit - Cursor) / (each > 0 ? each : 1);
				if (count < 0 || (std::size_t)count > room) {
					Fail();
					return(false);
				}
			}
			return(true);
		}

		/*
		 * Sizes a container the count asked for, failing the pass rather than throwing when
		 * the process cannot hold it. A count within the bytes remaining still asks for that
		 * many elements, which is more memory than the stream itself occupies, and for a
		 * wide element more than the container itself will hold.
		 */
		template<typename C>
		bool Reserve(C & container, int count)
		{
			if (count < 0 || (std::size_t)count > container.max_size()) {
				Fail();
				return(false);
			}

			try {
				container.clear();
				container.resize((std::size_t)count);
			} catch (std::bad_alloc const &) {
				container.clear();
				Fail();
				return(false);
			}
			return(true);
		}

		/*
		 * Numbers and enumerations travel as their declared width.
		 */
		template<typename T> requires (std::is_arithmetic_v<T> || std::is_enum_v<T>)
		void Serialize_Raw(T & value)
		{
			Serialize_Bytes(&value, sizeof(value));
		}

		/*
		 * A pointer travels as the identity the object it names announces on the way back
		 * in. Loading leaves the slot with the swizzle manager until the object says where
		 * it landed.
		 */
		template<SwizzleTarget T>
		void Serialize_Raw(T * & pointer, std::source_location const & where = std::source_location::current())
		{
			SwizzleIDType id = Is_Loading() ? 0 : Swizzler.ID_Of(pointer);
			Serialize_Bytes(&id, sizeof(id));

			if (Is_Loading() && !Was_Error()) {
				Swizzler.Swizzle((void **)&pointer, id, OwnerType, OwnerID, typeid(T).name(), where.file_name(), where.line());
			}
		}

		/*
		 * Anything that describes its own members.
		 */
		template<typename T> requires (HasSerializeMember<T> && !HasSerializeMemberWhere<T>)
		void Serialize_Raw(T & object)
		{
			if constexpr (SerializesPositionally<T>) {
				object.Serialize(*this);
			} else {
				Begin_Body();
				object.Serialize(*this);
				End_Body();
			}
		}

		/*
		 * The same, for one that wants the call site to report its elements against.
		 */
		template<typename T> requires HasSerializeMemberWhere<T>
		void Serialize_Raw(T & object, std::source_location const & where = std::source_location::current())
		{
			if constexpr (SerializesPositionally<T>) {
				object.Serialize(*this, where);
			} else {
				Begin_Body();
				object.Serialize(*this, where);
				End_Body();
			}
		}

		/*
		 * Arrays of a fixed size. Numbers go out in one block; anything else is
		 * serialized in place, one element at a time.
		 */
		template<typename T, int N>
		void Serialize_Raw(T (&array)[N], std::source_location const & where = std::source_location::current())
		{
			if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
				Serialize_Bytes(array, sizeof(array));
			} else {
				for (int index = 0; index < N; index++) {
					Serialize_Element(array[index], where);
				}
			}
		}

		// How much room a buffer keeps for its text is this build's business rather than
		// the file's, so only the text travels. The last byte stays the terminator, since
		// every reader of these buffers treats them as C strings. This claims every
		// char[N], so one holding bytes rather than text would be cut at its first zero.
		template<int N>
		void Serialize(char (&value)[N], std::source_location const & = std::source_location::current())
		{
			int count = 0;

			if (Is_Saving()) {
				while (count < N - 1 && value[count] != '\0') {
					count++;
				}
			}

			Serialize_Raw(count);

			if (Is_Loading() && (count < 0 || count >= N)) {
				Fail();
				return;
			}

			if (count > 0) {
				Serialize_Bytes(value, count);
			}

			if (Is_Loading()) {
				for (int index = count; index < N; index++) {
					value[index] = '\0';
				}
			}
		}

		/*
		 * The standard library's fixed size array travels as the built in one does.
		 */
		template<typename T, std::size_t N>
		void Serialize_Raw(std::array<T, N> & value, std::source_location const & where = std::source_location::current())
		{
			if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
				if constexpr (N > 0) {
					Serialize_Bytes(value.data(), (int)(sizeof(T) * N));
				}
			} else {
				for (std::size_t index = 0; index < N; index++) {
					Serialize_Element(value[index], where);
				}
			}
		}

		/*
		 * A vector travels as its length followed by its elements. Loading sizes it in
		 * full before any element registers the slot address it occupies.
		 */
		template<typename T> requires (!std::is_same_v<T, bool>)
		void Serialize_Raw(std::vector<T> & value, std::source_location const & where = std::source_location::current())
		{
			int count = (int)value.size();
			Serialize_Raw(count);

			if (Is_Loading()) {
				if (!Fits(count, (std::is_arithmetic_v<T> || std::is_enum_v<T>) ? sizeof(T) : 1)) {
					return;
				}
				if (!Reserve(value, count)) {
					return;
				}
			}

			if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
				if (count > 0) {
					Serialize_Bytes(value.data(), (int)(sizeof(T) * count));
				}
			} else {
				for (int index = 0; index < count; index++) {
					Serialize_Element(value[index], where);
				}
			}
		}

		/*
		 * An optional value is a flag followed by the value itself when there is one.
		 */
		template<typename T>
		void Serialize_Raw(std::optional<T> & value, std::source_location const & where = std::source_location::current())
		{
			bool present = value.has_value();
			Serialize_Raw(present);

			if (Is_Loading()) {
				value.reset();
				if (present) {
					value.emplace();
				}
			}

			if (present) {
				Serialize_Element(*value, where);
			}
		}

		/*
		 * A deque travels as a vector does, and is the safest of these to keep pointers
		 * in, since adding to either end leaves the elements already in it where they are.
		 */
		template<typename T>
		void Serialize_Raw(std::deque<T> & value, std::source_location const & where = std::source_location::current())
		{
			int count = (int)value.size();
			Serialize_Raw(count);

			if (Is_Loading()) {
				if (!Fits(count, 1)) {
					return;
				}
				if (!Reserve(value, count)) {
					return;
				}
			}

			for (int index = 0; index < count; index++) {
				Serialize_Element(value[index], where);
			}
		}

		/*
		 * A vector of bool holds its elements packed and hands out a proxy rather than an
		 * element, so each one is unpacked into an ordinary variable for the trip and put
		 * back afterwards. Assigning it back is harmless while saving.
		 */
		void Serialize_Raw(std::vector<bool> & value)
		{
			int count = (int)value.size();
			Serialize_Raw(count);

			if (Is_Loading()) {
				if (!Fits(count, 1) || !Reserve(value, count)) {
					return;
				}
			}

			for (int index = 0; index < count; index++) {
				bool element = value[(std::size_t)index];
				Serialize_Raw(element);
				value[(std::size_t)index] = element;
			}
		}

		/*
		 * A string travels as its length followed by its characters.
		 */
		void Serialize_Raw(std::string & value)
		{
			int count = (int)value.size();
			Serialize_Raw(count);

			if (Is_Loading()) {
				if (!Fits(count, 1) || !Reserve(value, count)) {
					return;
				}
			}

			if (count > 0) {
				Serialize_Bytes(value.data(), count);
			}
		}

		/*
		 * A pair travels as its two halves, in order.
		 */
		template<typename A, typename B>
		void Serialize_Raw(std::pair<A, B> & value, std::source_location const & where = std::source_location::current())
		{
			Serialize_Element(value.first, where);
			Serialize_Element(value.second, where);
		}

	private:
		/*
		 * One element of a container, handed the owning call site if it can take one --
		 * a container nested inside another included.
		 */
		template<typename T>
		void Serialize_Element(T & element, std::source_location const & where)
		{
			if constexpr (requires { Serialize_Raw(element, where); }) {
				Serialize_Raw(element, where);
			} else {
				Serialize_Raw(element);
			}
		}

		// A field is its identifier followed by its payload, which the table gives a width.
		struct BodyFrame
		{
			std::unordered_map<unsigned short, unsigned int> Fields;
			unsigned int End;
			unsigned int Limit;
		};

		std::vector<BodyFrame> Bodies;

		bool Open_Field(char const * name, unsigned char kind, unsigned int & mark, bool body = false);
		// Only a field the table gives no width to carries a length to fill in.
		void Close_Field(unsigned int mark, bool patch);
		bool Index_Body(BodyFrame & frame, unsigned int end);
		void Begin_Frame(bool indexed);
		void End_Frame(void);

		SaveNamesClass * Names;
		std::vector<unsigned char> * Buffer;
		unsigned int Cursor;

		// A read is judged against the innermost field or body rather than the whole
		// stream, so a field cannot spend the bytes of the one after it.
		unsigned int Limit;
		ModeType Mode;
		bool Failed;
		unsigned int FormatVersion;


		// A member reads from the position its identifier is recorded at, which is what
		// makes the order the file holds them in immaterial.


		/*
		 * The record this stream is carrying, named for the swizzle manager's report.
		 * Nothing on the save side needs it.
		 */
		char const * OwnerType;
		SwizzleIDType OwnerID;
};




/*
 * The version stamp of the save game currently being read, left here by the load as a
 * whole so every stream built during it reports the same version.
 */
extern unsigned int LoadedSaveVersion;
