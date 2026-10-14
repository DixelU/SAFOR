#include <iostream>
#include <string>
#include <stack>
#include <vector>
#include <fstream>
#include <iterator>
#include <array>
#include <thread>
#include <print>
#include <algorithm>
#include <memory>
#include <limits>
#include <bit>

#include <getopt.h>

#include "header_utils.h"

#ifdef _WIN32
#include "winapi_garbage.h"
#endif

#include "bbb_ffio.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-copy"
#pragma GCC diagnostic ignored "-Warray-bounds"
#include "btree/btree.h"
#include "btree/set.h"
#include "btree/map.h"
#pragma GCC diagnostic pop

using local_uint_t = std::uint64_t;
using smallest_tick_t = local_uint_t;			//Least top edge // Tick / len...
using track_n_t = std::uint32_t;					//track number type
using parameter_t = std::uint8_t;

constexpr local_uint_t VELOCITY_MASK = 0xFFFFFFFFFFFFFFULL;
constexpr std::uint32_t MTHD = 1297377380;
constexpr std::uint32_t MTRK = 1297379947;

// todo: pipe mode;
bool pipe_mode = false;

bool quiet_mode = false;
bool quietest_mode = false;

bool velocity_mode = false;
bool sustains_removal = false;
unsigned char min_velocity = 0;

constexpr bool dbg = true;

local_uint_t note_count = 0, pushed_count = 0, total_count = 0;

#pragma pack(push, 4)
struct NoteObject
{
	smallest_tick_t tick;
	std::uint32_t length;
	track_n_t track;
	parameter_t key;
	mutable std::uint8_t velocity;
};
#pragma pack(pop)

bool operator<(const NoteObject& a, const NoteObject& b)
{
	if (a.tick < b.tick)
		return true;

	if (a.tick == b.tick)
		return a.key < b.key;

	return false;
}

bool priority_predicate(const NoteObject& O, const NoteObject& N)
{
	//old and new //false - save both//true - replace O with N
	if (O.key != N.key || O.tick != N.tick)
		return false;

	if (O.key == 0xFF && N.track < O.track)
		return false;

	if (N.track > O.track && N.length < O.length)
		return false;

	if (N.track == O.track && N.length < O.length)
		return false;

	if (O.velocity > N.velocity)
		N.velocity = O.velocity;

	return true;
}

// Notes packed as tick deltas and varints (~7 bytes per note), written once and then read once.
// Blocks are freed as soon as they are read, so moving notes from one stream
// into another never needs memory for both copies at the same time.
class note_stream
{
	struct block
	{
		std::unique_ptr<std::uint8_t[]> data;
		std::uint32_t capacity;
		std::uint32_t used;
	};

	static constexpr std::size_t min_block_size = 256;
	static constexpr std::size_t max_block_size = 64 * 1024;
	// tick delta + key + velocity + length + track
	static constexpr std::size_t max_record_size = 10 + 1 + 1 + 5 + 5;

	std::vector<block> blocks;
	std::size_t read_block = 0;
	std::size_t read_pos = 0;
	smallest_tick_t push_tick = 0;
	smallest_tick_t pop_tick = 0;
	std::size_t notes_left = 0;
	std::size_t bytes_left = 0;
	std::size_t bytes_pushed = 0;

	static void put_varint(std::uint8_t*& ptr, std::uint64_t value)
	{
		while (value >= 0x80)
		{
			*ptr++ = static_cast<std::uint8_t>(value | 0x80);
			value >>= 7;
		}
		*ptr++ = static_cast<std::uint8_t>(value);
	}

	static std::uint64_t get_varint(const std::uint8_t*& ptr)
	{
		std::uint64_t value = 0;
		int shift = 0;
		std::uint8_t byte;

		do
		{
			byte = *ptr++;
			value |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
			shift += 7;
		}
		while (byte & 0x80);

		return value;
	}

public:
	[[nodiscard]] bool empty() const { return notes_left == 0; }
	[[nodiscard]] std::size_t size() const { return notes_left; }
	[[nodiscard]] std::size_t bytes() const { return bytes_left; }

	// works with any order of ticks, but only sorted ones give small deltas
	void push(const NoteObject& note)
	{
		if (blocks.empty() || blocks.back().capacity - blocks.back().used < max_record_size)
		{
			// small streams stay small, big ones end up with big blocks
			const auto capacity = std::clamp(bytes_pushed, min_block_size, max_block_size);
			blocks.push_back({
				std::unique_ptr<std::uint8_t[]>(new std::uint8_t[capacity]),
				static_cast<std::uint32_t>(capacity),
				0});
		}

		auto& last = blocks.back();
		auto* const begin = last.data.get() + last.used;
		auto* ptr = begin;

		put_varint(ptr, note.tick - push_tick);
		*ptr++ = note.key;
		*ptr++ = note.velocity;
		put_varint(ptr, note.length);
		put_varint(ptr, note.track);
		push_tick = note.tick;

		const auto written = static_cast<std::size_t>(ptr - begin);
		last.used += written;
		bytes_pushed += written;
		bytes_left += written;
		notes_left++;
	}

	// returns false once every note has been read
	bool pop(NoteObject& note)
	{
		if (!notes_left)
			return false;

		if (read_pos == blocks[read_block].used)
		{
			blocks[read_block].data.reset();
			read_block++;
			read_pos = 0;
		}

		const auto* const begin = blocks[read_block].data.get() + read_pos;
		const auto* ptr = begin;

		note.tick = (pop_tick += get_varint(ptr));
		note.key = *ptr++;
		note.velocity = *ptr++;
		note.length = static_cast<std::uint32_t>(get_varint(ptr));
		note.track = static_cast<track_n_t>(get_varint(ptr));

		const auto read = static_cast<std::size_t>(ptr - begin);
		read_pos += read;
		bytes_left -= read;
		notes_left--;
		return true;
	}
};

struct OverlapsRemover
{
	// raw notes are collected into chunks of this size, then sorted and packed into a run
	static constexpr std::size_t chunk_size = 1 << 18;
	static constexpr std::size_t read_buffer_size = 1 << 22;
	// runs get folded into the survivors once they take at least this much space...
	static constexpr std::size_t min_runs_bytes = 16 << 20;
	// ...and at least 1/N of the space the survivors take
	static constexpr std::size_t survivors_to_runs_ratio = 2;

	parameter_t rsb_byte;
	std::uint16_t ppq_value;
	track_n_t current_track;

	// notes which survived so far, ordered by (tick, key, insertion order)
	note_stream note_set;
	// notes pushed since the last compaction:
	// each run is sorted by (tick, key), the runs themselves follow insertion order
	std::vector<note_stream> runs;
	std::size_t runs_bytes;
	// the newest notes, in insertion order
	std::vector<NoteObject> chunk;
	std::vector<NoteObject> chunk_buffer;

	// the first 128 is the first channel, next 128 are the second...
	std::array<std::vector<local_uint_t>, 2048> poly;
	bbb_ffr* file_input;

	OverlapsRemover():
		rsb_byte(0),
		ppq_value(0),
		current_track(0),
		runs_bytes(0),
		file_input(nullptr)
	{}

	static void ostream_write(
		std::vector<std::uint8_t>& vec,
		std::ostream& out)
	{
		out.write(reinterpret_cast<char*>(vec.data()), vec.size());
	}

	void clear_polyphony()
	{
		for (auto& polyphony_stack: poly)
			polyphony_stack.clear();
	}

	void initialize(const std_unicode_string& link)
	{
		file_input = new bbb_ffr(link.c_str(), read_buffer_size);
		if (!quietest_mode)
			std::print("File buffer size: {}\n", file_input->tell_bufsize());

		std::uint32_t MThd = 0;
		for (int i = 0; i < 4; i++)
			MThd = (MThd << 8) | (file_input->get());

		if (MThd == MTHD)
		{
			// skip header entirely
			for (int i = 0; i < 8; i++)
				file_input->get();

			for (int i = 0; i < 2; i++)
				ppq_value = (ppq_value << 8) | (file_input->get());

			note_set = note_stream();
			runs.clear();
			runs_bytes = 0;
			chunk.clear();
			rsb_byte = 0;
			current_track = 0;
		}
		else
		{
			file_input->close();

			if (!quietest_mode)
				std::print("Input file doesn't begin with MThd\n");
		}
	}

	// returns whether reading shall continue or not
	bool read_single_track()
	{
		rsb_byte = 0;

		std::uint32_t MTrk = 0;
		local_uint_t current_tick = 0;
		std::uint32_t delta_time = 0;

		clear_polyphony();

		// seek the header no matter the junk we've found:
		for (int i = 0; i < 4 && file_input->good(); i++)
			MTrk = (MTrk << 8) | file_input->get();
		while (MTrk != MTRK && file_input->good())
			MTrk = (MTrk << 8) | file_input->get();

		for (int i = 0; i < 4 && file_input->good(); i++)
			file_input->get(); // iterating through track's length

		while (file_input->good())
		{
			delta_time = read_vlv();
			if (!parse_event(current_tick += delta_time))
				return true;
		}

		return false;
	}

	void smart_push(const NoteObject& event)
	{
		if (sustains_removal && event.key < 0xFF && event.length == 0)
			return;

		total_count++;
		if (velocity_mode && event.velocity < min_velocity && event.key < 0xFF)
			return;

		pushed_count++;

		// overlaps are resolved later, in bulk, by compact()
		chunk.push_back(event);
		if (chunk.size() >= chunk_size)
			flush_chunk();
	}

	void flush_chunk()
	{
		if (chunk.empty())
			return;

		sort_chunk();

		note_stream run;
		for (const auto& note : chunk)
			run.push(note);
		chunk.clear();

		runs_bytes += run.bytes();
		runs.push_back(std::move(run));

		if (runs_bytes >= std::max(min_runs_bytes, note_set.bytes() / survivors_to_runs_ratio))
			compact();
	}

	// Stable LSD radix sort by (tick, key), so notes of the same (tick, key) keep the order they were pushed in.
	// Several times faster than std::stable_sort here, since ticks of a chunk span few bits.
	void sort_chunk()
	{
		const auto [min_note, max_note] = std::ranges::minmax_element(chunk, {}, &NoteObject::tick);
		const auto min_tick = min_note->tick;
		const int key_bits = std::bit_width(max_note->tick - min_tick) + 8;

		if (key_bits > 64)
		{
			std::stable_sort(chunk.begin(), chunk.end());
			return;
		}

		const auto sort_key = [min_tick](const NoteObject& note)
		{
			return ((note.tick - min_tick) << 8) | note.key;
		};

		constexpr int digit_bits = 11;
		constexpr std::uint64_t digit_mask = (1 << digit_bits) - 1;
		std::array<std::size_t, 1 << digit_bits> offsets;

		chunk_buffer.resize(chunk.size());

		for (int shift = 0; shift < key_bits; shift += digit_bits)
		{
			offsets.fill(0);
			for (const auto& note : chunk)
				offsets[(sort_key(note) >> shift) & digit_mask]++;

			// every note has the same digit here, nothing to move
			if (offsets[(sort_key(chunk.front()) >> shift) & digit_mask] == chunk.size())
				continue;

			std::size_t offset = 0;
			for (auto& count : offsets)
				offset += std::exchange(count, offset);

			for (const auto& note : chunk)
				chunk_buffer[offsets[(sort_key(note) >> shift) & digit_mask]++] = note;

			chunk.swap(chunk_buffer);
		}
	}

	// pushes the notes of all runs against the survivors
	void compact()
	{
		std::vector<note_stream*> inputs { &note_set };
		for (auto& run : runs)
			inputs.push_back(&run);

		note_set = merge(inputs, !velocity_mode);
		runs.clear();
		runs_bytes = 0;
	}

	// removes notes of the group the new one overrides, then adds the new one;
	// the same thing smart_push used to do against the whole note set
	void fold_note(std::vector<NoteObject>& group, const NoteObject& note)
	{
		std::size_t kept = 0;
		for (std::size_t i = 0; i < group.size(); i++)
		{
			if (priority_predicate(group[i], note))
			{
				total_count--;
				continue;
			}

			group[kept++] = group[i];
		}

		group.resize(kept);
		group.push_back(note);
	}

	// Merges sorted streams into one; notes of the same (tick, key) follow the order of the inputs.
	// With `fold`, the first input must hold survivors,
	// and the notes of every other input get pushed against them one by one.
	note_stream merge(const std::vector<note_stream*>& inputs, bool fold)
	{
		note_stream result;
		std::vector<NoteObject> heads(inputs.size());
		std::vector<std::size_t> heap;

		// std heap keeps the "largest" element on top, so this puts the earliest head there
		const auto heap_order = [&heads](std::size_t a, std::size_t b)
		{
			if (heads[b] < heads[a])
				return true;

			if (heads[a] < heads[b])
				return false;

			return b < a;
		};

		for (std::size_t i = 0; i < inputs.size(); i++)
			if (inputs[i]->pop(heads[i]))
				heap.push_back(i);

		std::ranges::make_heap(heap, heap_order);

		// notes of the current (tick, key)
		std::vector<NoteObject> group;

		while (!heap.empty())
		{
			std::ranges::pop_heap(heap, heap_order);
			const auto index = heap.back();
			heap.pop_back();

			// keep taking notes from this input for as long as it stays ahead of all the others
			bool has_more;
			do
			{
				NoteObject note = heads[index];
				has_more = inputs[index]->pop(heads[index]);

				if (!group.empty() && (group.front() < note || note < group.front()))
				{
					for (const auto& grouped : group)
						result.push(grouped);

					group.clear();
				}

				if (fold && index != 0)
					fold_note(group, note);
				else
					group.push_back(note);
			}
			while (has_more && (heap.empty() || heap_order(heap.front(), index)));

			if (has_more)
			{
				heap.push_back(index);
				std::ranges::push_heap(heap, heap_order);
			}
		}

		for (const auto& grouped : group)
			result.push(grouped);

		return result;
	}

	[[nodiscard]] std::uint32_t read_vlv() const
	{
		if (!file_input->good())
			return 0;

		std::uint32_t value = 0;
		std::uint8_t byte = 0;

		do
		{
			byte = file_input->get();
			value = (value << 7) | (byte & 0x7F);
		}
		while (byte & 0x80);

		return value;
	}

	local_uint_t pop_note_on(smallest_tick_t pos, local_uint_t requested_data)
	{
		if (poly[pos].empty())
		{
			// current tick with volume = 1
			return requested_data | (VELOCITY_MASK + 1);
		}

		// isn't it the opposite?
		const auto data = poly[pos].back();
		poly[pos].pop_back();

		return data;
	}

	// returns whether the current track can be read further
	bool parse_event(local_uint_t current_tick)
	{
		if (!file_input->good())
			return false;

		auto event_header = file_input->get();

		// RSB handling
		uint8_t first_param = 0;
		if (event_header < 0x80 )
		{
			first_param = event_header;
			event_header = rsb_byte;
		}
		else if (event_header < 0xF0)
			first_param = file_input->get();

		if (event_header >= 0x80 && event_header <= 0x8F)
		{
			rsb_byte = event_header;
			note_count++;

			const auto key = first_param & 0x7F;

			//position of stack for this key/channel pair
			const auto pos = ((rsb_byte & 0x0F) << 7) | key;

			// aftertouch doesn't matter
			file_input->get();

			NoteObject note;
			note.key = key;
			note.track = (rsb_byte & 0x0F) | ((current_track) << 4);
			if (poly[pos].empty())
			{
				// log something here?
				return true;
			}

			const auto note_on_data =
				pop_note_on(pos, current_tick);

			note.tick = note_on_data & VELOCITY_MASK;
			note.length = current_tick - note.tick;
			note.velocity = note_on_data >> 56;

			smart_push(note);
		}
		else if (event_header >= 0x90 && event_header <= 0x9F)
		{
			rsb_byte = event_header;

			const std::uint8_t key = first_param & 0x7F;
			const std::uint8_t volume = file_input->get() & 0x7F;

			//position of stack for this key/channel pair
			const auto pos = ((rsb_byte & 0x0F) << 7) | key;

			if (volume != 0)
			{
				poly[pos].push_back(current_tick | (static_cast<local_uint_t>(volume) << 56));
				return true;
			}

			// otherwise this is a note off event
			NoteObject note;
			note.key = key;
			note.track = (rsb_byte & 0x0F) | ((current_track) << 4);

			if (poly[pos].empty())
			{
				// log something here?
				return true;
			}

			const auto note_on_data =
				pop_note_on(pos, current_tick);

			note.tick = note_on_data & VELOCITY_MASK;
			note.length = current_tick - note.tick;
			note.velocity = note_on_data >> 56;

			smart_push(note);
		}
		else if ((event_header >= 0xA0 && event_header <= 0xBF) || (event_header >= 0xE0 && event_header <= 0xEF))
		{
			rsb_byte = event_header;

			file_input->get();
		}
		else if (event_header >= 0xC0 && event_header <= 0xDF)
		{
			rsb_byte = event_header;
		}
		else if (event_header >= 0xF0 && event_header <= 0xF7)
		{
			rsb_byte = 0;

			const auto vlv = read_vlv();
			for (auto i = 0u; i < vlv; i++)
				file_input->get();
		}
		else if (event_header == 0xFF)
		{
			rsb_byte = 0;

			const auto meta_kind = file_input->get();
			std::uint32_t meta_length = read_vlv();

			if (meta_kind == 0x2F)
			{
				// merge tracks here?
				return false;
			}

			if (meta_kind == 0x51)
			{
				std::uint32_t tempo_data = 0;
				for (auto i = 0u; i < meta_length; i++)
				{
					// tempo change data
					auto byte = file_input->get();
					tempo_data = (tempo_data << 8) | byte;
				}

				// 0xFF key is "mapped" tempo event data
				NoteObject event;
				event.key = 0xFF;
				event.track = 0;
				event.tick = current_tick;
				event.length = tempo_data;
				event.velocity = 0;

				smart_push(event);
			}
			else
				for (auto i = 0u; i < meta_length; i++)
					file_input->get();
		}
		else
		{
			if (!quietest_mode)
				std::cerr << "Invalid data\n\tat: " <<
					static_cast<unsigned int>(rsb_byte) << "; " <<
					static_cast<unsigned int>(event_header) << "; "
					"at 0x" << std::hex << file_input->tellg() << std::dec << std::endl ;

			// bruh;
			std::uint8_t I = 0, II = 0, III = 0;
			while (!(I == 0xFF && II == 0x2F && III == 0) && !file_input->eof())
			{
				I = II;
				II = III;
				III = file_input->get();
			}

			return false;
		}

		return true;
	}

	struct OutputTrack
	{
		note_stream notes;
		std::size_t events_count = 0;
	};

	// splits the survivors by track, keeping their order
	std::vector<std::unique_ptr<OutputTrack>> single_pass_filter()
	{
		constexpr local_uint_t log_trigger = 5000000;
		local_uint_t dump_counter = 0;

		if (!quiet_mode)
			std::print("Single pass scan has started... it might take a while...\n");

		local_uint_t _counter = 0;

		// indexed by track number
		std::vector<std::unique_ptr<OutputTrack>> tracks;

		NoteObject note;
		while (note_set.pop(note))
		{
			if (note.track >= tracks.size())
				tracks.resize(note.track + 1);

			auto& track = tracks[note.track];
			if (!track)
				track = std::make_unique<OutputTrack>();

			track->notes.push(note);

			if (note.key == 0xFF)
			{
				track->events_count++;
				continue;
			}

			// Note ON and Note OFF events
			track->events_count += 2;

			_counter++;
			if (_counter >= log_trigger)
			{
				// damn tdm gcc ...
				if (!quiet_mode)
					std::print("Single pass scan: {} notes put\n", dump_counter + _counter);

				dump_counter += _counter;
				_counter = 0;
			}
		}
		note_set = note_stream();

		if (!quietest_mode)
			std::print("Single pass scan has finished... Note count: {}\n", dump_counter + _counter);

		return tracks;
	}

	// Converts the notes of a single track back to MIDI events and writes them out.
	// Note offs wait in a heap until their tick comes, which gives the same event order
	// the old per-track multisets had: by tick, then by the order the events were added in.
	static void write_track(note_stream& notes, std::ostream& fout)
	{
		struct NoteOff
		{
			smallest_tick_t tick;
			std::size_t order;
			std::uint8_t status;
			std::uint8_t key;
		};

		// std heap keeps the "largest" element on top, so this puts the earliest note off there
		const auto later = [](const NoteOff& a, const NoteOff& b)
		{
			if (a.tick != b.tick)
				return a.tick > b.tick;

			return a.order > b.order;
		};

		constexpr std::size_t flush_size = 1 << 20;

		std::vector<NoteOff> note_offs;
		std::vector<std::uint8_t> track_data;
		smallest_tick_t previous_tick = 0;
		std::uint64_t track_size = 0;

		const auto flush = [&]()
		{
			ostream_write(track_data, fout);
			track_size += track_data.size();
			track_data.clear();
		};

		const auto push_delta = [&](smallest_tick_t tick)
		{
			const std::uint32_t tTick = tick - previous_tick;
			push_vlv(tTick, track_data);
			previous_tick = tick;
		};

		const auto push_note_offs = [&](smallest_tick_t until)
		{
			while (!note_offs.empty() && note_offs.front().tick <= until)
			{
				std::ranges::pop_heap(note_offs, later);
				const auto& note_off = note_offs.back();

				push_delta(note_off.tick);
				track_data.push_back(note_off.status);
				track_data.push_back(note_off.key);
				track_data.push_back(0x40);

				note_offs.pop_back();
			}
		};

		// the size is not known yet, it gets patched in at the end
		const auto header_pos = fout.tellp();
		fout.write("MTrk\0\0\0\0", 8);

		std::size_t order = 0;
		NoteObject note;
		while (notes.pop(note))
		{
			push_note_offs(note.tick);
			push_delta(note.tick);

			if (note.key == 0xFF)
			{
				track_data.push_back(0xFF);
				track_data.push_back(0x51);
				track_data.push_back(0x03);
				track_data.push_back((note.length & 0xFF0000) >> 16);
				track_data.push_back((note.length & 0xFF00) >> 8);
				track_data.push_back(note.length & 0xFF);
			}
			else
			{
				const std::uint8_t channel = note.track & 0xF;

				track_data.push_back(0x90 | channel);
				track_data.push_back(note.key);
				track_data.push_back((note.velocity) ? note.velocity : 1);

				note_offs.push_back({note.tick + note.length, order++, static_cast<std::uint8_t>(0x80 | channel), note.key});
				std::ranges::push_heap(note_offs, later);
			}

			if (track_data.size() >= flush_size)
				flush();
		}

		push_note_offs(std::numeric_limits<smallest_tick_t>::max());

		track_data.push_back(0x00);
		track_data.push_back(0xFF);
		track_data.push_back(0x2F);
		track_data.push_back(0x00);
		flush();

		const std::uint32_t size = track_size;
		const auto end_pos = fout.tellp();
		fout.seekp(header_pos + std::streamoff(4));
		fout.put(static_cast<char>((size & 0xFF000000) >> 24));
		fout.put(static_cast<char>((size & 0xFF0000) >> 16));
		fout.put(static_cast<char>((size & 0xFF00) >> 8));
		fout.put(static_cast<char>(size & 0xFF));
		fout.seekp(end_pos);
	}

	static uint8_t push_vlv(uint32_t value, std::vector<std::uint8_t>& vec)
	{
		constexpr uint8_t $7byte_mask = 0x7F, max_size = 5, $7byte_mask_size = 7;
		constexpr uint8_t $adjacent7byte_mask = ~$7byte_mask;

		uint8_t stack[max_size];
		uint8_t size = 0;
		uint8_t r_size = 0;

		do {
			stack[size] = (value & $7byte_mask);
			value >>= $7byte_mask_size;
			if (size)
				stack[size] |= $adjacent7byte_mask;
			size++;
		} while (value);

		r_size = size;
		while (size)
			vec.push_back(stack[--size]);

		return r_size;
	};

	void write_midi(const std_unicode_string& path, std_unicode_string save_path_override)
	{
		if (!quietest_mode)
			std::print("Starting enhanced output algorithm\n");

		auto tracks = single_pass_filter();

		size_t tracks_count = std::ranges::count_if(tracks, [](const auto& track) { return track != nullptr; });
		if (!quietest_mode)
			std::print("Tracks used: {}\n", tracks_count);

		if (save_path_override.empty())
		{
			std_unicode_string suffix;
			if (velocity_mode)
				suffix = to_cchar_t(".AR.mid").operator std_unicode_string();
			else if (sustains_removal)
				suffix = to_cchar_t(".SOR.mid").operator std_unicode_string();
			else
				suffix = to_cchar_t(".OR.mid").operator std_unicode_string();

			save_path_override = path + suffix;
		}

		auto [pfstr, c_file] = open_wide_stream<std::ostream>(
			save_path_override, to_cchar_t("wb"));

		std::ostream& fout = *pfstr;

		fout.put('M');
		fout.put('T');
		fout.put('h');
		fout.put('d');
		fout.put(0);
		fout.put(0);
		fout.put(0);
		fout.put(6);
		fout.put(0);
		fout.put(1);
		fout.put(static_cast<char>((tracks_count >> 8)));
		fout.put(static_cast<char>((tracks_count & 0xFF)));
		fout.put(static_cast<char>(ppq_value >> 8));
		fout.put(static_cast<char>(ppq_value & 0xFF));

		for (std::size_t track_id = 0; track_id < tracks.size(); track_id++)
		{
			auto& track = tracks[track_id];
			if (!track)
				continue;

			if (!quiet_mode)
				std::print("Current track size: {}\n"
					"\tConverting track back to MIDI standard\n",
					track->events_count);

			write_track(track->notes, fout);
			track.reset();

			if (dbg && !quiet_mode)
				std::print("Track {} went to output\n", track_id);
		}

		fout.flush();
	}

	void notes_remapping() // sustain removal - sparse version
	{
		if (note_set.empty())
			return;

		// Group notes by key in a single pass - O(n) instead of O(128*n)
		std::array<note_stream, 128> notes_by_key;
		// Keep tempo events (key == 0xFF) and other special events
		note_stream other_events;

		NoteObject note;
		while (note_set.pop(note))
		{
			if (note.key < 128)
				notes_by_key[note.key].push(note);
			else
				other_events.push(note);
		}
		note_set = note_stream();

		if (!quiet_mode)
			std::print("Single-pass grouping complete\n");

		struct NoteEnd
		{
			local_uint_t tick;
			std::uint32_t order;
		};

		// std heap keeps the "largest" element on top, so this puts the earliest end there
		const auto later = [](const NoteEnd& a, const NoteEnd& b)
		{
			if (a.tick != b.tick)
				return a.tick > b.tick;

			return a.order > b.order;
		};

		std::vector<NoteEnd> note_ends;
		std::array<note_stream, 128> remapped;

		for (int key = 0; key < 128; key++)
		{
			auto& key_notes = notes_by_key[key];
			if (key_notes.empty())
				continue;

			if (!quiet_mode)
				std::print("Processing key {} with {} notes (sparse)\n", key, key_notes.size());

			// Match the original dense implementation: later notes in the per-key stream
			// overwrite earlier ones for every covered tick.
			btree::map<std::uint32_t, std::pair<std::uint32_t, std::uint8_t>> active;

			// Current segment tracking
			local_uint_t segment_start = 0;
			std::uint32_t segment_track = 0;
			std::uint8_t segment_velocity = 0;

			struct OwnerState
			{
				std::uint32_t order;
				std::uint32_t track;
				std::uint8_t velocity;
			};

			auto get_owner = [&]() -> OwnerState
			{
				if (active.empty())
					return {0, 0, 1};

				const auto it = active.rbegin();
				return {it->first, it->second.first, it->second.second};
			};

			// Note starts come straight from the stream, which is ordered by (tick, order);
			// note ends wait in a heap until their tick comes.
			std::uint32_t order = 0;
			NoteObject next;
			bool has_next = key_notes.pop(next);
			note_ends.clear();

			while (has_next || !note_ends.empty())
			{
				const auto tick = (!note_ends.empty() && (!has_next || note_ends.front().tick < next.tick)) ?
					note_ends.front().tick :
					next.tick;

				std::uint32_t started_owner_order = 0;
				bool has_started_owner = false;
				uint8_t max_vel = 0;

				// Taking every start of a tick before its ends leaves the same state behind
				// as going through them sorted by (order, start before end) does.
				while (has_next && next.tick == tick)
				{
					active[order] = {next.track, next.velocity};
					started_owner_order = order;
					has_started_owner = true;

					max_vel = max_vel < next.velocity ? next.velocity : max_vel;

					note_ends.push_back({next.tick + next.length, order});
					std::ranges::push_heap(note_ends, later);

					++order;
					has_next = key_notes.pop(next);
				}

				while (!note_ends.empty() && note_ends.front().tick == tick)
				{
					active.erase(note_ends.front().order);
					std::ranges::pop_heap(note_ends, later);
					note_ends.pop_back();
				}

				const auto owner = get_owner();
				const auto new_track = owner.track;

				// Keep the same edge detection as the dense timeline painter:
				// split when the visible track changes or when the now-visible owner starts here.
				bool owner_changed = (new_track != segment_track);
				bool new_note_on =
					has_started_owner &&
					owner.order == started_owner_order &&
					(tick > segment_start);

				if (owner_changed || new_note_on)
				{
					// Emit previous segment if valid
					if (segment_track != 0 && tick > segment_start)
					{
						NoteObject segment;
						segment.key = key;
						segment.tick = segment_start;
						segment.length = tick - segment_start;
						segment.track = segment_track;
						segment.velocity = segment_velocity;
						remapped[key].push(segment);
					}

					segment_start = tick;
					segment_track = new_track;

					// Revived notes inherit no note-on edge at this tick, matching the dense mapper.
					segment_velocity = new_note_on ? max_vel : 1;
				}
			}

			if (!quiet_mode)
				std::print("Key {} processed in sustains removing algorithm (sparse)\n", key);
		}

		// segments of a key never share a tick, so this restores (tick, key) order unambiguously
		std::vector<note_stream*> inputs;
		for (auto& key_notes : remapped)
			inputs.push_back(&key_notes);
		inputs.push_back(&other_events);

		note_set = merge(inputs, false);
	}

	void process(const std_unicode_string& path, const std_unicode_string& save_path_override)
	{
		initialize(path);

		if (!quiet_mode)
			std::print("    Notes read yet   :  Total objects count : Overlaps-removed objects count\n");

		current_track = 2;
		while (read_single_track())
		{
			current_track++;
			if (!quiet_mode)
				std::print("{:20} : {:20} : {:20}\n", note_count, pushed_count, total_count);
		}

		file_input->close();

		// neither the read buffer nor the polyphony stacks are needed anymore
		delete file_input;
		file_input = nullptr;
		for (auto& polyphony_stack : poly)
			std::vector<local_uint_t>().swap(polyphony_stack);

		flush_chunk();
		if (!runs.empty())
			compact();
		std::vector<NoteObject>().swap(chunk);
		std::vector<NoteObject>().swap(chunk_buffer);

		if (dbg && !quietest_mode)
			std::print("Load finished with set size of {}...\n", note_set.size());

		if (dbg && !quiet_mode && sustains_removal)
			std::print("Note count might increase after remapping the MIDI\n");

		if (sustains_removal)
			notes_remapping();

		if (dbg && !quiet_mode)
			std::print("Ready for output...\n");

		write_midi(path, save_path_override);
	}
};

#ifdef __WIN32__
std_unicode_string open_file_dialog(const cchar_t* title)
{
	auto mask = to_cchar_t("MIDI Files(*.mid)\0*.mid\0");

	OPENFILENAMEW ofn;
	cchar_t filepath_buffer[1000];
	ZeroMemory(&ofn, sizeof(ofn));
	ZeroMemory(filepath_buffer, sizeof(filepath_buffer));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = filepath_buffer;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(filepath_buffer) / sizeof(cchar_t);
	ofn.lpstrFilter = mask;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = nullptr;
	ofn.lpstrTitle = title;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = nullptr;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;

	if (GetOpenFileNameW(&ofn))
		return {filepath_buffer};

	return to_cchar_t("");
}

int main__windows_runtime()
{
	std_unicode_string filename;
	while(winapi_garbage::GetMode() < 0) {}

	velocity_mode = (winapi_garbage::removal_mode_line == 2);
	sustains_removal = (winapi_garbage::removal_mode_line == 1);

	if (velocity_mode)
	{
		while(winapi_garbage::GetThreshold() < 0) {}
		min_velocity = winapi_garbage::velocity_threshold;
	}

	if (velocity_mode && !quietest_mode)
		std::print("SAFOR. Art removing mode. Overlaps and sustains are not removed.\n");
	else if (sustains_removal && !quietest_mode)
		std::print("SAFSOR. Note remapping enabled. Audio may alter in unexpected ways.\n");
	else if (!quietest_mode)
		std::print("SAFOR. Classic Edition.\n");

	if (!quietest_mode)
		std::print("\"Open file\" dialog should appear soon...\n");

	while ((filename = open_file_dialog(to_cchar_t("Select MIDI File."))).empty()) {}

	if (!filename.empty())
	{
		OverlapsRemover worker;

		if (!quiet_mode)
		{
			std::print("Filename in ASCII: ");
			for (const auto& ch : filename)
				std::cout << static_cast<char>(ch);
			std::cout << std::endl;
		}

		worker.process(filename, to_cchar_t(""));
	}

	system("pause");
	return 0;
}
#endif

constexpr int GRACEFUL_DENY = 255;
constexpr int INCORRECT_CLI_OPTS = -1;

void print_usage(const char* program_name)
{
	std::cout
		<< "Usage: " << program_name << " [OPTIONS] target.midi\n\n"
		<< "Options:\n"
		<< "  -o, --overlaps         Overlaps removal (Default behavior)\n"
		<< "  -s, --sustains         Remove sustains (Implies -o)\n"
		<< "  -v, --velocity <num>   Velocity threshold (Excludes -o and -s)\n"
		<< "  -q, --quiet            Quiet mode\n"
		<< "  -Q, --quitest          Very quiet mode (Implies -q)\n"
		<< "  -r, --redirect         <path> Optional save path\n"
		<< "  -h, --help             Show this help message\n";
}

int main__cli_runtime(int argc, char** &argv)
{
	if (argc < 2)
		return GRACEFUL_DENY;

	bool overlaps_removal_set = false;
	std_unicode_string redirect_path;
	std_unicode_string target_file;

	// 1. Definition of long options
	// layout: { "long-name", argument_requirement, flag_ptr, short-char }
	const option long_options[] =
	{
		{"overlaps",	no_argument,		nullptr, 'o'},
		{"sustains",	no_argument,		nullptr, 's'},
		{"velocity",	required_argument,	nullptr, 'v'},
		{"quiet",	no_argument,		nullptr, 'q'},
		{"quitest",	no_argument,		nullptr, 'Q'},
		{"redirect",	required_argument,	nullptr, 'r'},
		{"help",	no_argument,		nullptr, 'h'},
		{nullptr,	0,			nullptr, '\0'}
		// ^Sentinel to mark end of array
	};

	int opt;
	int option_index = 0;

	auto argv_string_to_unicode_string = [](const char* str) -> std_unicode_string
	{
		std_unicode_string string;
		while (*str != '\0')
			string.push_back(*str++);
		return string;
	};

	// 2. Parsing Loop
	// The string "osv:qQr:h" defines short options.
	// A colon ( : ) after a character means it requires an argument.
	while ((opt = getopt_long(argc, argv, "osv:qQr:h", long_options, &option_index)) != -1)
	{
		switch (opt) {
		case 'o':
			overlaps_removal_set = true;
			break;
		case 's':
			sustains_removal = true;
			break;
		case 'v':
			velocity_mode = true;

			try
			{
				min_velocity = std::stoi(optarg);
			}
			catch (...)
			{
				std::print(stderr,"Error: -v requires a valid byte argument.\n");
				return INCORRECT_CLI_OPTS;
			}

			break;
		case 'q':
			quiet_mode = true;
			break;
		case 'Q':
			quietest_mode = true;
			break;
		case 'r':
		{
			redirect_path = argv_string_to_unicode_string(optarg);
			break;
		}
		case 'h':
			print_usage(argv[0]);
			return 0;
		case '?':
			// getopt_long already prints an error message for unknown options
			print_usage(argv[0]);
			return INCORRECT_CLI_OPTS;
		default:
			return GRACEFUL_DENY;
		}
	}

	if (optind < argc)
	{
		target_file = argv_string_to_unicode_string(argv[optind]);
		optind++;
	}
	else
	{
		std::print(stderr, "Error: Missing required target file.\n");
		print_usage(argv[0]);
		return INCORRECT_CLI_OPTS;
	}

	if (optind < argc)
	{
		std::print(stderr, "Error: Too many arguments provided.\n");
		print_usage(argv[0]);
		return INCORRECT_CLI_OPTS;
	}

	if (quietest_mode)
		quiet_mode = true;

	if (sustains_removal)
		overlaps_removal_set = true;

	if (velocity_mode && (overlaps_removal_set || sustains_removal))
	{
		std::print(stderr, "Error: Option -v is incompatible with -o or -s.\n");
		return INCORRECT_CLI_OPTS;
	}

	OverlapsRemover worker;
	worker.process(target_file, redirect_path);
	return 0;
}

int main(int argc, char** argv)
{
	int result = main__cli_runtime(argc, argv);
	if (result != GRACEFUL_DENY)
		return result;

#ifdef __WIN32__
	return main__windows_runtime();
#else
	print_usage(argv[0]);
	return 0;
#endif
}
