#include <iostream>
#include <string>
#include <stack>
#include <vector>
#include <fstream>
#include <iterator>
#include <array>
#include <set>
#include <thread>

#include "winapi_garbage.h"

#include "bbb_ffio.h"

#include "btree/btree.h"
#include "btree/set.h"
#include "btree/map.h"

using local_uint_t = std::uint64_t;
using smallest_tick_t = local_uint_t;			//Least top edge // Tick / len...
using track_n_t = std::uint32_t;					//track number type
using parameter_t = std::uint8_t;

constexpr local_uint_t VELOCITY_MASK = 0xFFFFFFFFFFFFFFULL;
constexpr std::uint32_t MTHD = 1297377380;
constexpr std::uint32_t MTRK = 1297379947;

//#pragma pack(push, 1)

bool velocity_mode = false;
bool sustains_removal = false;
unsigned char min_velocity = 0;

constexpr bool dbg = true;

local_uint_t note_count = 0, pushed_count = 0, total_count = 0;

struct NoteObject
{
	smallest_tick_t tick;
	std::uint32_t length;
	parameter_t key;
	track_n_t track;
	mutable std::uint8_t velocity;
};

struct RawEvent
{
	smallest_tick_t tick;
	std::uint8_t a, b, c, d;
};

struct TrackSymbol
{
	smallest_tick_t tick;
	std::uint32_t length;
	std::uint32_t track;
	parameter_t velocity;
};

bool operator<(const RawEvent& a, const RawEvent& b)
{
	return a.tick < b.tick;
}

bool operator<(const NoteObject& a, const NoteObject& b)
{
	if (a.tick < b.tick)
		return true;

	if (a.tick == b.tick)
		return a.key < b.key;

	return false;
}

bool operator<(const TrackSymbol& a, const TrackSymbol& b)
{
	return a.tick < b.tick;
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

std::ostream& operator<<(std::ostream& stream, const NoteObject& a)
{
	return
		stream <<
			"K" << static_cast<int>(a.key) <<
			"L" << a.length <<
			"T" << a.tick <<
			"TN" << a.track;
}

struct OverlapsRemover
{
	parameter_t rsb_byte;
	std::uint16_t ppq_value;
	track_n_t current_track;

	std::multiset<NoteObject> note_set;
	btree::multiset<track_n_t> tracks_set;
	btree::map<track_n_t, btree::multiset<RawEvent>> mapped_notes_set;

	// the first 128 is the first channel, next 128 are the second... etc.
	std::array<std::deque<local_uint_t>, 2048> poly;
	bbb_ffr* file_input;

	OverlapsRemover():
		rsb_byte(0),
		ppq_value(0),
		current_track(0),
		file_input(nullptr)
	{}

	static void ostream_write(
		std::vector<std::uint8_t>& vec,
		const std::vector<std::uint8_t>::iterator& beg,
		const std::vector<std::uint8_t>::iterator& end,
		std::ostream& out)
	{
		const auto offset = beg - vec.begin();
		out.write(
			reinterpret_cast<char*>(vec.data()) + offset,
			end - beg);
	}

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

	void initialize(const std::wstring& link)
	{
		file_input = new bbb_ffr(link.c_str());
		std::cout << "File buffer size: " << file_input->tell_bufsize() << std::endl;

		std::uint32_t MThd = 0;
		for (int i = 0; i < 4; i++)
			MThd = (MThd << 8) | (file_input->get());

		std::cout << (file_input->eof() ? "EOF" : "File opened") << std::endl;
		if (MThd == MTHD)
		{
			// skip header entirely
			for (int i = 0; i < 8; i++)
				file_input->get();

			for (int i = 0; i < 2; i++)
				ppq_value = (ppq_value << 8) | (file_input->get());

			note_set.clear();
			rsb_byte = 0;
			current_track = 0;

			if (dbg)
				printf("Header\n");
		}
		else
		{
			file_input->close();
			std::cout << "Input file doesn't begin with MThd" << std::endl;
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
		for (int i = 0; i < 4 && !file_input->eof() && file_input->good(); i++)
			MTrk = (MTrk << 8) | file_input->get();
		while (MTrk != MTRK && !file_input->eof() && file_input->good())
			MTrk = (MTrk << 8) | file_input->get();

		for (int i = 0; i < 4 && !file_input->bad(); i++)
			file_input->get(); // iterating through track's length

		while (!file_input->bad() && !file_input->eof())
		{
			delta_time = read_vlv();
			if (!parse_event(current_tick += delta_time))
				return true;
		}

		return false;
	}

	//for debug purposes
	[[nodiscard]] std::uint32_t get_total_poly() const
	{
		std::uint32_t N = 0;
		for (auto i = 0; i < 2048; i++)
			N += poly[i].size();

		return N;
	}

	void smart_push(const NoteObject& event)
	{
		total_count++;
		if(velocity_mode)
		{
			if (event.velocity < min_velocity && event.key < 0xFF)
				return;

			pushed_count++;
			note_set.insert(event);
			return;
		}

		pushed_count++;

		auto [ begin, end ] = note_set.equal_range(event);
		if (!note_set.empty() && begin != note_set.end())
		{
			auto current_iter = begin;
			const auto rightmost = std::prev(end);

			while (current_iter != end && !(*current_iter < *rightmost) && !(*rightmost < *current_iter))
			{
				if (!priority_predicate(*current_iter, event))
				{
					++current_iter;
					continue;
				}

				current_iter = note_set.erase(current_iter);

				total_count--;
			}
		}

		note_set.insert(end, event);
	}

	[[nodiscard]] std::uint32_t read_vlv() const
	{
		if (file_input->eof() || file_input->bad())
		{
			if (false)
				std::cout << "Failed to read vlv at " << file_input->tellg() << std::endl;
			return 0;
		}

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
			if (false)
				std::cout << "pop error " << pos << " " << requested_data << std::endl;

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
		if (file_input->bad() || file_input->eof())
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

				smart_push(event);
			}
			else
				for (auto i = 0u; i < meta_length; i++)
					file_input->get();
		}
		else
		{
			std::cout << "Invalid data\n\tat: " <<
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

	void single_pass_filter()
	{
		constexpr local_uint_t log_trigger = 5000000;
		local_uint_t dump_counter = 0;

		std::cout << "Single pass scan has started... it might take a while...\n";
		local_uint_t _counter = 0;

		auto iter = note_set.begin();
		while (iter != note_set.end())
		{
			RawEvent event;
			auto& Note = *iter;
			auto& track_ref = mapped_notes_set[Note.track];

			if (Note.key == 0xFF)
			{
				event.tick = Note.tick;
				event.a = 0x03;
				event.b = (Note.length & 0xFF0000) >> 16;
				event.c = (Note.length & 0xFF00) >> 8;
				event.d = (Note.length & 0xFF);
				track_ref.insert(event);
			}
			else
			{
				// Note ON event
				event.tick = Note.tick;
				event.a = 0;
				event.b = 0x90 | (Note.track & 0xF);
				event.c = Note.key;
				event.d = ((Note.velocity) ? Note.velocity : 1);
				track_ref.insert(event);

				// Note OFF event
				event.tick += Note.length;
				event.b ^= 0x10;
				event.d = 0x40;
				track_ref.insert(event);

				_counter++;
				if (_counter >= log_trigger)
				{
					printf("Single pass scan: %llu notes put\n", dump_counter + _counter);
					dump_counter += _counter;
					_counter = 0;
				}
			}
			iter = note_set.erase(iter);
		}
		note_set.clear();

		std::cout << "Single pass scan has finished... Note count: " << dump_counter + _counter << std::endl;
	}

	static uint8_t push_vlv(uint32_t value, std::vector<BYTE>& vec)
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

	void write_midi(const std::wstring& path)
	{
		printf("Starting enhanced output algorithm\n");
		single_pass_filter();
		std::vector<std::uint8_t> track_data;

		auto pfstr = open_wide_stream<std::ostream>((path + (
			(velocity_mode)?
			L".AR.mid" :
			((sustains_removal) ? L".SOR.mid" : L".OR.mid" )
			)), L"wb");
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
		fout.put(static_cast<char>((tracks_set.size() >> 8)));
		fout.put(static_cast<char>((tracks_set.size() & 0xFF)));
		fout.put(static_cast<char>(ppq_value >> 8));
		fout.put(static_cast<char>(ppq_value & 0xFF));

		tracks_set.clear();

		auto note_maps_iter = mapped_notes_set.begin();
		while (note_maps_iter != mapped_notes_set.end())
		{
			track_data.push_back('M');
			track_data.push_back('T');
			track_data.push_back('r');
			track_data.push_back('k');
			track_data.push_back(0); // size
			track_data.push_back(0); // of
			track_data.push_back(0); // track
			track_data.push_back(0); // placeholder

			if (dbg)
				printf("Current track size: %lld\n\tConverting track back to MIDI standard\n", note_maps_iter->second.size());

			smallest_tick_t previous_tick = 0;
			auto iter = note_maps_iter->second.begin();
			while (iter != note_maps_iter->second.end())
			{
				auto event = *iter;
				const std::uint32_t tTick = event.tick - previous_tick;

				/*auto length = */push_vlv(tTick, track_data);
				if (event.a == 0x03)
				{
					track_data.push_back(0xFF);
					track_data.push_back(0x51);
					track_data.push_back(0x03);
					track_data.push_back(event.b);
					track_data.push_back(event.c);
					track_data.push_back(event.d);
				}
				else
				{
					track_data.push_back(event.b);
					track_data.push_back(event.c);
					track_data.push_back(event.d);
				}

				previous_tick = event.tick;
				++iter; //= pMS->erase(U);
			}
			note_maps_iter->second.clear();

			track_data.push_back(0x00);
			track_data.push_back(0xFF);
			track_data.push_back(0x2F);
			track_data.push_back(0x00);

			std::uint32_t size = track_data.size() - 8;
			track_data[4] = (size & 0xFF000000) >> 24;
			track_data[5] = (size & 0xFF0000) >> 16;
			track_data[6] = (size & 0xFF00) >> 8;
			track_data[7] = (size & 0xFF);
			ostream_write(track_data, fout);

			if (dbg)
				printf("Track %u went to output\n", note_maps_iter->first);
			track_data.clear();

			++note_maps_iter;
		}

		fout.flush();
	}

	void notes_remapping() // sustain removal
	{
		std::vector<std::uint32_t> single_key_data;
		std::vector<TrackSymbol> key_vector;

		local_uint_t size;

		if (note_set.empty())
			return;

		for (int key = 0; key < 128; key++)
		{
			NoteObject note;
			note.key = key;
			note.velocity = 1;

			auto iter = note_set.begin();
			local_uint_t furthest_tick = 0;

			while (iter != note_set.end())
			{
				if (iter->key != key)
				{
					++iter;
					continue;
				}

				const auto note_ending = iter->tick + iter->length;
				if(furthest_tick < note_ending)
					furthest_tick = note_ending;

				TrackSymbol insertable;
				insertable.tick = iter->tick;
				insertable.track = iter->track;
				insertable.length = iter->length;
				insertable.velocity = iter->velocity;

				key_vector.push_back(insertable);
				iter = note_set.erase(iter);
			}

			if (key_vector.empty())
				continue;

			printf("Set traversal ended with %llu keys\n", key_vector.size());
			printf("Expected extra memory consumption: %llu\n bytes", furthest_tick);

			furthest_tick++; // important for note-off event detection.

			if (furthest_tick >= single_key_data.size())
			{
				single_key_data.resize(furthest_tick, 0);
				printf("Key map expansion %llu\n", single_key_data.size());
			}

			for (auto & track_symbol : key_vector)
			{
				size = track_symbol.tick + track_symbol.length;
				auto& current_tick_data = single_key_data[track_symbol.tick];

				const auto velocity = (std::max)(
					static_cast<unsigned char>((current_tick_data >> 1) & 0xFF),
					track_symbol.velocity);
				current_tick_data = (track_symbol.track << (1 + 8)) | (velocity << 1) | 1;

				for (local_uint_t tick = track_symbol.tick + 1; tick < size; ++tick)
					single_key_data[tick] = ((track_symbol.track << (1 + 8)) /*| (velocity << 1)*/);
			}

			printf("Key map traversal ended\n");
			key_vector.clear();

			local_uint_t index = 0;
			local_uint_t last_detected_edge = 0;
			size = single_key_data.size();

			// todo: sparse remap for 32k ppq, high bpm midis (foreshadowing)
			while (index < size)
			{
				//LastEdge = T;
				for (++index; index < size; ++index)
				{
					if ((single_key_data[index] >> (1 + 8)) != (single_key_data[index - 1] >> (1 + 8)) || (single_key_data[index] & 1))
					{
						note.length = index - last_detected_edge;
						note.tick = last_detected_edge;
						note.track = (single_key_data[index - 1] >> (1 + 8));
						note.velocity = ((single_key_data[last_detected_edge] >> 1) & 0xFF);
						last_detected_edge = index;
						if (note.track)
							note_set.insert(note);

						break;
					}
				}
			}
			single_key_data.clear();

			printf("Key %d processed in sustains removing algorithm\n", key);
		}
	}

	void process(const std::wstring& path)
	{
		initialize(path);

		printf("Note count : Successfully pushed notes (Count) : Notes and tempo count without overlaps\n");

		current_track = 2;
		while (read_single_track())
		{
			current_track++;
			std::cout << note_count << " : " << pushed_count << " : " << total_count << std::endl;
		}
		file_input->close();

		if (dbg)
			printf("Magic finished with set size %lld...\n", note_set.size());
		if (dbg && sustains_removal)
			printf("Note count might increase after remapping the MIDI\n");
		if (sustains_removal)
			notes_remapping();

		auto iter = note_set.begin();
		while (iter != note_set.end())
		{
			if (tracks_set.find(iter->track) == tracks_set.end())
				tracks_set.insert((iter->track));
			++iter;
		}

		if (dbg)
			printf("Ready for output...\n");
		std::cout << "Tracks used: " << tracks_set.size() << std::endl;

		write_midi(path);
	}
};

std::wstring open_file_dialog(const wchar_t* Title)
{
	OPENFILENAMEW ofn;
	wchar_t filepath_buffer[1000];
	ZeroMemory(&ofn, sizeof(ofn));
	ZeroMemory(filepath_buffer, 1000);
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = filepath_buffer;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(filepath_buffer);
	ofn.lpstrFilter = L"MIDI Files(*.mid)\0*.mid\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = nullptr;
	ofn.lpstrTitle = Title;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = nullptr;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;
	if (GetOpenFileNameW(&ofn))
		return {filepath_buffer};

	return L"";
}

int main(int argc, char** argv)
{
	while(winapi_garbage::GetMode() < 0);

	velocity_mode = (winapi_garbage::RemovalModeLine == 2);
	sustains_removal = (winapi_garbage::RemovalModeLine == 1);

	if(velocity_mode)
	{
		if (argc <= 1)
		{
			while(winapi_garbage::GetThreshold() < 0);
			min_velocity = winapi_garbage::VelocityThreshold;
		}
		else
			min_velocity = std::stoi(std::string(argv[1]));

		printf("SAFOR. Art removing mode. Overlaps and sustains are not removed.\n");
	}
	else
	{
		if (sustains_removal)
			printf("SAFSOR. Note remapping enabled. Audio may alter in unexpected ways.\n");
		else
			printf("SAFOR. Classic Edition.\n");
	}

	std::cout << "\"Open file\" dialog should appear soon...\n";
	std::wstring filename;
	while ((filename = open_file_dialog(L"Select MIDI File.")).empty());
	if (!filename.empty())
	{
		OverlapsRemover worker;
		std::cout << "Filename in ASCII: ";
		for (const auto& ch : filename)
			std::cout << static_cast<char>(ch);

		std::cout << std::endl;
		worker.process(filename);
	}

	system("pause");
	return 0;
}
