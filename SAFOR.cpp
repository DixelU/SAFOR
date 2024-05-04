#include <iostream>
#include <set>
#include <string>
#include <stack>
#include <vector>
#include <fstream>
#include <iterator>
#include <list>
#include <map>
#include <thread>

#include "winapi_garbage.h"

#include "bbb_ffio.h"

#include "btree/btree.h"
#include "btree/btree_set.h"
#include "btree/btree_map.h"

using UnsigedLongInt = unsigned long long int;
using LeastTopEdge = UnsigedLongInt;			//Least top edge // Tick / len...
using TrackNumberType = DWORD;					//track number type
using KeyDataType = BYTE;						//key data type

constexpr auto VOLUMEMASK = ((UnsigedLongInt)0xFFFFFFFFFFFFFF);
constexpr DWORD MTHD = 1297377380;
constexpr DWORD MTRK = 1297379947;

//#pragma pack(push, 1)

bool VelocityMode = false;
bool RemovingSustains = false;
unsigned char MinimalVolume = 0;

bool dbg = 1;

UnsigedLongInt Notecount = 0, PushedCount = 0, OverlapslessCount = 0;

struct NoteObject { //
	LeastTopEdge Tick;
	KeyDataType Key;//0xWWQQ ww-findable/seekable state, QQ-key itself
	mutable BYTE Vol;
	TrackNumberType TrackN;
	DWORD Len;
};
struct PrepairedEvent {
	LeastTopEdge Tick;
	BYTE A, B, C, D;
};
struct TrackSymbol {
	LeastTopEdge Tick;
	DWORD Len;
	DWORD TrackN;
	KeyDataType Velocity;
};
bool operator<(const PrepairedEvent& a, const PrepairedEvent& b) {
	if (a.Tick < b.Tick)return 1;
	else return 0;
}
bool operator<(const NoteObject& a, const NoteObject& b) {
	if (a.Tick < b.Tick)return 1;
	else if (a.Tick == b.Tick) {
		if (a.Key < b.Key)return 1;
		else return 0;
	}
	else return 0;
}
bool operator<(const TrackSymbol& a, const TrackSymbol& b) {
	if (a.Tick < b.Tick)return 1;
	else return 0;
}
bool PriorityPredicate(const NoteObject& O, NoteObject& N) { //old and new//0 - save both//1 replce O with N
	if (O.Key != N.Key || O.Tick != N.Tick)return 0;//
	else if (O.Key == 0xFF && N.TrackN < O.TrackN)return 0;
	else if (N.TrackN > O.TrackN && N.Len < O.Len)return 0;
	else if (N.TrackN == O.TrackN && N.Len < O.Len)return 0;
	else if (O.Vol > N.Vol) N.Vol = O.Vol;
	return 1;
}
std::ostream& operator<<(std::ostream& stream, const NoteObject& a) {
	return (stream << "K" << (int)a.Key << "L" << a.Len << "T" << a.Tick << "TN" << a.TrackN);
}

struct OverlapsRemover {
	BYTE RunningStatus;
	WORD PPQN;
	DWORD CurrentTrack;
	btree::btree_multiset<NoteObject> NoteSet;
	btree::btree_multiset<TrackNumberType> TracksSet;
	btree::btree_map<DWORD, btree::btree_multiset<PrepairedEvent>> MappedNotesSet;
	std::array<std::deque<UnsigedLongInt>, 2048> Polyphony;//first 128 is first channel, next 128 are the second... etc
	bbb_ffr* FileInput;
	OverlapsRemover() {
		RunningStatus = PPQN = CurrentTrack = 0;
	}
	static void ostream_write(std::vector<BYTE>& vec, const std::vector<BYTE>::iterator& beg,
		const std::vector<BYTE>::iterator& end, std::ostream& out) {
		out.write(((char*)vec.data()) + (beg - vec.begin()), end - beg);
	}
	static void ostream_write(std::vector<BYTE>& vec, std::ostream& out) {
		out.write(((char*)vec.data()), vec.size());;
	}
	void ClearPolyphony() {
		for (auto& polyphonyStack: Polyphony)
			polyphonyStack.clear();
	}
	void InitializeNPrepare(std::wstring link) {
		FileInput = new bbb_ffr(link.c_str());
		std::cout << "File buffer size: " << FileInput->tell_bufsize() << std::endl;
		DWORD MThd = 0;
		BYTE ByteVariable;
		for (int i = 0; i < 4; i++) {
			ByteVariable = FileInput->get();
			MThd = (MThd << 8) | (ByteVariable);
		}
		std::cout << (FileInput->eof() ? "EOF" : "File opened") << std::endl;
		if (MThd == MTHD) {
			for (int i = 0; i < 8; i++)
				FileInput->get();
			for (int i = 0; i < 2; i++) {
				ByteVariable = (*FileInput).get();
				PPQN = (PPQN << 8) | (ByteVariable);
			}
			NoteSet.clear();
			RunningStatus = 0;
			CurrentTrack = 0;
			if (dbg)
				printf("Header\n");
		}
		else {
			(*FileInput).close();
			std::cout << "Input file doesn't begin with MThd" << std::endl;
		}
	}
	bool ReadSingleTrackFromCurPos() { //continue?
		DWORD MTrk = 0;
		RunningStatus = 0;
		UnsigedLongInt CurrentTick = 0;//current tick
		DWORD RealTick = 0;//real tick (which we just read)
		ClearPolyphony();
		for (int i = 0; i < 4 && !(*FileInput).eof() && (*FileInput).good(); i++)
			MTrk = (MTrk << 8) | (*FileInput).get();
		while (MTrk != MTRK && !(*FileInput).eof() && (*FileInput).good()) { 
			MTrk = (MTrk << 8) | (*FileInput).get();
		}
		for (int i = 0; i < 4 && !(*FileInput).bad(); i++)
			(*FileInput).get(); //itterating through track's lenght
		while (!(*FileInput).bad() && !(*FileInput).eof()) {
			RealTick = ReadVLV();
			if (!ParseEvent(CurrentTick += RealTick))
				return 1;
		}
		return 0;
	}
	DWORD CountMomentalPolyphony() { //debug purposes
		DWORD N = 0;
		for (int i = 0; i < 2048; i++)
			N += Polyphony[i].size();
		return N;
	}
	void PushNote(NoteObject& Ev) {
		OverlapslessCount++;
		if(VelocityMode) {
			if (Ev.Vol < MinimalVolume && Ev.Key < 0xFF) 
				return;
		}
		else {
			PushedCount++;
			auto e_pair = NoteSet.equal_range(Ev);
			if (NoteSet.size() && e_pair.first != NoteSet.end()) {
				auto& current_p = e_pair.first;
				auto rightmost = *(--e_pair.second);
				while (current_p != NoteSet.end() && (!(*current_p < rightmost) && !(rightmost < *current_p))) {
					if (PriorityPredicate(*current_p, Ev)) {
						current_p = NoteSet.erase(current_p);
						OverlapslessCount--;
					}
					else
						current_p++;
				}
			}
		}
		NoteSet.insert(Ev);
	}
	DWORD ReadVLV() { //from current position
		DWORD VLV = 0;
		BYTE B = 0;
		if (!(*FileInput).eof() && !(*FileInput).bad()) {
			do {
				B = (*FileInput).get();
				VLV = (VLV << 7) | (B & 0x7F);
			} while (B & 0x80);
			return VLV;
		}
		else {
			if (0)std::cout << "Failed to read VLV at " << (*FileInput).tellg() << std::endl;
			return 0;
		}
	}
	UnsigedLongInt FindAndPopOut(LeastTopEdge pos, UnsigedLongInt CTick) {
		UnsigedLongInt q = Polyphony[pos].size();
		if (q > 0) {
			q = (Polyphony[pos].front());
			Polyphony[pos].pop_front();
			return q;
		}
		else {
			if (0)
				std::cout << "FaPO error " << pos << " " << CTick << std::endl;
			return CTick | (VOLUMEMASK + 1);
		}
	}
	bool ParseEvent(UnsigedLongInt absTick) { //should we continue?
		BYTE ByteVar1, ByteVar2;
		LeastTopEdge pos;
		UnsigedLongInt FAPO;
		if (!(*FileInput).bad() && !(*FileInput).eof()) {
			ByteVar1 = (*FileInput).get();
			if (ByteVar1 >= 0x80 && ByteVar1 <= 0x8F) { //NOTEOFF
				RunningStatus = ByteVar1;
				Notecount++;
				ByteVar1 = (*FileInput).get() & 0x7F;
				pos = ((RunningStatus & 0x0F) << 7) | ByteVar1;//position of stack for this key/channel pair
				(*FileInput).get();
				NoteObject Event;//event push prepairings
				Event.Key = ByteVar1;
				Event.TrackN = (RunningStatus & 0x0F) | ((CurrentTrack) << 4);
				if (!Polyphony[pos].empty()) {
					FAPO = FindAndPopOut(pos, absTick);
					Event.Tick = FAPO & VOLUMEMASK;
					Event.Len = absTick - Event.Tick;
					Event.Vol = FAPO >> 56;
					PushNote(Event);
				}
				else if (0)std::cout << "Detected empty stack pop-attempt (N):" << (unsigned int)(RunningStatus & 0x0F) << '-' << (unsigned int)ByteVar1 << std::endl;
			}
			else if (ByteVar1 >= 0x90 && ByteVar1 <= 0x9F) { //NOTEON
				RunningStatus = ByteVar1;
				ByteVar1 = (*FileInput).get() & 0x7F;
				ByteVar2 = (*FileInput).get() & 0x7F;
				pos = ((RunningStatus & 0x0F) << 7) | ByteVar1;
				if (ByteVar2 != 0)
					Polyphony[pos].push_front(absTick | (((UnsigedLongInt)ByteVar2) << 56));
				else { //quite weird way to represent note off event...
					NoteObject Event;//event push prepairings
					Event.Key = ByteVar1;
					Event.TrackN = (RunningStatus & 0x0F) | ((CurrentTrack) << 4);
					if (!Polyphony[pos].empty()) {
						FAPO = FindAndPopOut(pos, absTick);
						Event.Tick = FAPO & VOLUMEMASK;
						Event.Len = absTick - Event.Tick;
						Event.Vol = FAPO >> 56;
						PushNote(Event);
					}
					else if (0)std::cout << "Detected empty stack pop-attempt (0):" << (RunningStatus & 0x0F) << '-' << (unsigned int)ByteVar1 << std::endl;
				}
			}
			else if ((ByteVar1 >= 0xA0 && ByteVar1 <= 0xBF) || (ByteVar1 >= 0xE0 && ByteVar1 <= 0xEF)) { //stupid unusual vor visuals stuff
				RunningStatus = ByteVar1;
				(*FileInput).get();
				(*FileInput).get();
			}
			else if (ByteVar1 >= 0xC0 && ByteVar1 <= 0xDF) {
				RunningStatus = ByteVar1;
				(*FileInput).get();
			}
			else if (ByteVar1 >= 0xF0 && ByteVar1 <= 0xF7) {
				RunningStatus = 0;
				DWORD vlv = ReadVLV();
				for (int i = 0; i < vlv; i++)(*FileInput).get();
			}
			else if (ByteVar1 == 0xFF) {
				RunningStatus = 0;
				ByteVar1 = (*FileInput).get();
				DWORD vlv = 0;
				if (ByteVar1 == 0x2F) {
					//if(dbg)printf("endoftrack\n");
					ReadVLV();
					return 0;
				}
				else if (ByteVar1 == 0x51) {
					(*FileInput).get();//vlv
					for (int i = 0; i < 3; i++) { //tempochange data
						ByteVar1 = (*FileInput).get();
						vlv = (vlv << 8) | ByteVar1;
					}//in vlv we have tempo data :)

					NoteObject Event;//event push prepairings
					Event.Key = 0xFF;
					Event.TrackN = 0;
					Event.Tick = absTick;
					Event.Len = vlv;
					PushNote(Event);
				}
				else {
					vlv = ReadVLV();
					for (int i = 0; i < vlv; i++)
						(*FileInput).get();
				}
			}
			else {
				if (RunningStatus >= 0x80 && RunningStatus <= 0x8F) { //NOTEOFF
					Notecount++;
					(*FileInput).get();//same
					pos = ((RunningStatus & 0x0F) << 7) | ByteVar1;//position of stack for this key/channel pair
					NoteObject Event;//event push prepairings
					Event.Key = ByteVar1;
					Event.TrackN = (RunningStatus & 0x0F) | ((CurrentTrack) << 4);
					if (!Polyphony[pos].empty()) {
						FAPO = FindAndPopOut(pos, absTick);
						Event.Tick = FAPO & VOLUMEMASK;
						Event.Len = absTick - Event.Tick;
						Event.Vol = FAPO >> 56;
						PushNote(Event);
					}
					else if (0)std::cout << "Detected empty stack pop-attempt (RN):" << (unsigned int)(RunningStatus & 0x0F) << '-' << (unsigned int)ByteVar1 << std::endl;
				}
				else if (RunningStatus >= 0x90 && RunningStatus <= 0x9F) { //NOTEON
					ByteVar2 = (*FileInput).get() & 0x7F;//magic finished//volume
					pos = ((RunningStatus & 0x0F) << 7) | ByteVar1;
					if (ByteVar2 != 0)
						Polyphony[pos].push_front(absTick | (((UnsigedLongInt)ByteVar2) << 56));
					else { //quite weird way to represent note off event...
						NoteObject Event;//event push prepairings
						Event.Key = ByteVar1;
						Event.TrackN = (RunningStatus & 0x0F) | ((CurrentTrack) << 4);
						if (!Polyphony[pos].empty()) {
							FAPO = FindAndPopOut(pos, absTick);
							Event.Tick = FAPO & VOLUMEMASK;
							Event.Len = absTick - Event.Tick;
							Event.Vol = FAPO >> 56;
							PushNote(Event);
						}
						else if (0)std::cout << "Detected empty stack pop-attempt (R0):" << (unsigned int)(RunningStatus & 0x0F) << '-' << (unsigned int)ByteVar1 << std::endl;
					}
				}
				else if ((RunningStatus >= 0xA0 && RunningStatus <= 0xBF) || (RunningStatus >= 0xE0 && RunningStatus <= 0xEF)) { //stupid unusual for visuals stuff
					(*FileInput).get();
				}
				else if (RunningStatus >= 0xC0 && RunningStatus <= 0xDF) { }
				else {
					std::cout << "Imparseable data...\n\tdebug:" << (unsigned int)RunningStatus << ":" << (unsigned int)ByteVar1 << ":Off(FBegin):";
					printf("%x\n", (*FileInput).tellg());
					BYTE I = 0, II = 0, III = 0;
					while (!(I == 0xFF && II == 0x2F && III == 0) && !(*FileInput).eof()) {
						I = II;
						II = III;
						III = (*FileInput).get();
					}
					(*FileInput).get();
					return 0;
				}
			}
		}
		else return 0;
		return 1;
	}
	void SinglePassMapFiller() {
		const UnsigedLongInt EDGE_LOGGER = 5000000;
		UnsigedLongInt DumpCounter = 0;
		std::cout << "Single pass scan has started... it might take a while...\n";
		auto Y = NoteSet.begin();
		UnsigedLongInt _Counter = 0;
		NoteObject Note;//prev out, out
		while (Y != NoteSet.end()) {
			PrepairedEvent Event;
			Note = *Y;
			if (!(Note.Key ^ 0xFF)) {
				Event.Tick = Note.Tick;
				Event.A = 0x03;
				Event.B = (Note.Len & 0xFF0000) >> 16;
				Event.C = (Note.Len & 0xFF00) >> 8;
				Event.D = (Note.Len & 0xFF);
				MappedNotesSet[Note.TrackN].insert(Event);
			}
			else {
				Note.Key &= 0xFF;
				Event.Tick = Note.Tick;//noteon event
				Event.A = 0;
				Event.B = 0x90 | (Note.TrackN & 0xF);
				Event.C = Note.Key;
				Event.D = ((Note.Vol) ? Note.Vol : 1);
				MappedNotesSet[Note.TrackN].insert(Event);
				Event.Tick += Note.Len;//note off event
				Event.B ^= 0x10;
				Event.D = 0x40;
				MappedNotesSet[Note.TrackN].insert(Event);
				_Counter++;
				if (_Counter >= EDGE_LOGGER) {
					printf("Single pass scan: %u note\n", _Counter);
					DumpCounter += _Counter;
					_Counter = 0;
				}
			}
			Y = NoteSet.erase(Y);
		}
		NoteSet.clear();
		std::cout << "Single pass scan has finished... Notecount: " << DumpCounter + _Counter << std::endl;
	}
	inline uint8_t push_vlv(uint32_t value, std::vector<BYTE>& vec) {
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
	void FormMIDI(std::wstring Link) {
		printf("Starting enhanced output algorithm\n");
		SinglePassMapFiller();
		std::vector<BYTE> Track;
		
		auto pfstr = open_wide_stream<std::ostream>((Link + (
			(VelocityMode)?
			L".AR.mid" :
			((RemovingSustains) ? L".SOR.mid" : L".OR.mid" )
			)), L"wb");
		std::ostream& fout = *pfstr;
		auto Y = MappedNotesSet.begin();
		btree::btree_multiset<PrepairedEvent>::iterator U;
		btree::btree_multiset<PrepairedEvent>* pMS;
		PrepairedEvent Event, PrevEvent;
		if (dbg)
			printf("Output..\n");
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
		fout.put((char)((TracksSet.size() >> 8)));
		fout.put((char)((TracksSet.size() & 0xFF)));
		fout.put((char)(PPQN >> 8));
		fout.put((char)(PPQN & 0xFF));
		TracksSet.clear();
		if (dbg)printf("Header...\n");
		while (Y != MappedNotesSet.end()) {
			Track.push_back('M');
			Track.push_back('T');
			Track.push_back('r');
			Track.push_back('k');
			Track.push_back(0);//size
			Track.push_back(0);//of
			Track.push_back(0);//track
			Track.push_back(0);//aslkflkasdflksdf
			if (dbg)printf("Track header...\nCurrent track size: %d\n", (*Y).second.size());
			pMS = &((*Y).second);
			U = pMS->begin();
			Event.Tick = 0;
			if (dbg)printf("Converting back to MIDI standard\n");
			while (U != pMS->end()) {
				PrevEvent = Event;
				Event = *U;
				DWORD tTick = Event.Tick - PrevEvent.Tick, clen = 0;
				push_vlv(tTick, Track);
				if (Event.A == 0x03) {
					Track.push_back(0xFF);
					Track.push_back(0x51);
					Track.push_back(Event.A);//03
					Track.push_back(Event.B);
					Track.push_back(Event.C);
					Track.push_back(Event.D);
				}
				else {
					Track.push_back(Event.B);
					Track.push_back(Event.C);
					Track.push_back(Event.D);
				}
				U++; //= pMS->erase(U);
			}
			pMS->clear();
			Track.push_back(0x00);
			Track.push_back(0xFF);
			Track.push_back(0x2F);
			Track.push_back(0x00);
			DWORD sz = Track.size() - 8;
			Track[4] = (sz & 0xFF000000) >> 24;
			Track[5] = (sz & 0xFF0000) >> 16;
			Track[6] = (sz & 0xFF00) >> 8;
			Track[7] = (sz & 0xFF);
			ostream_write(Track, fout);
			if (dbg)printf("Track %u went to output\n", (*Y).first);
			Track.clear();
			Y++;
		}
		fout.flush();
	}
	void MapNotesAndReadBack() {
		std::vector<DWORD> PERKEYMAP;
		std::vector<TrackSymbol> KEYVEC;
		NoteObject ImNote;
		UnsigedLongInt T, size, LastEdge = 0;
		if (!NoteSet.size()) return;
		for (int key = 0; key < 128; key++) {
			ImNote.Key = key;
			ImNote.Vol = 1;
			auto Y = NoteSet.begin();
			UnsigedLongInt furthest_tick = 0;
			while (Y != NoteSet.end()) {
				if ((*Y).Key == key) {
					auto EndPosition = (*Y).Tick + (*Y).Len;
					if(furthest_tick < EndPosition)
						furthest_tick = EndPosition;
						
					TrackSymbol VecInsertable;
					VecInsertable.Tick = (*Y).Tick;
					VecInsertable.TrackN = (*Y).TrackN;
					VecInsertable.Len = (*Y).Len;
					VecInsertable.Velocity = (*Y).Vol;
					
					KEYVEC.push_back(VecInsertable);
					Y = NoteSet.erase(Y);
					continue;
				}
				else {
					Y++;
				}
			}
			if (KEYVEC.empty())
				continue;
			printf("Set traveral ended with %u keys\n", KEYVEC.size());
			printf("Expected size: %u\n", furthest_tick); // hell
			furthest_tick++; // important for note-off event detection. 
			if (furthest_tick >= PERKEYMAP.size()) {
				PERKEYMAP.resize(furthest_tick, 0);
				printf("Key map expansion %u\n", PERKEYMAP.size());
			}
			for (auto it = KEYVEC.begin(); it != KEYVEC.end(); ++it) {
				size = (*it).Tick + (*it).Len;
				
				auto& currentTickData = PERKEYMAP[(*it).Tick];
				auto velocity = (std::max)(
					(unsigned char)((currentTickData >> 1) & 0xFF), 
					(*it).Velocity);
				currentTickData = ((*it).TrackN << (1 + 8)) | (velocity << 1) | 1;
				
				for (UnsigedLongInt tick = (*it).Tick + 1; tick < size; ++tick)
					PERKEYMAP[tick] = (((*it).TrackN << (1 + 8)) /*| (velocity << 1)*/);
			}
			printf("Key map traversal ended\n");
			KEYVEC.clear();
			T = 0;
			LastEdge = 0;
			size = PERKEYMAP.size();
			while (T < size) {
				//LastEdge = T;
				for (++T; T < size; ++T) {
					if ((PERKEYMAP[T] >> (1 + 8)) != (PERKEYMAP[T - 1] >> (1 + 8)) || (PERKEYMAP[T] & 1)) {
						ImNote.Len = T - LastEdge;
						ImNote.Tick = LastEdge;
						ImNote.TrackN = (PERKEYMAP[T - 1] >> (1 + 8));
						ImNote.Vol = ((PERKEYMAP[LastEdge] >> 1) & 0xFF);
						LastEdge = T;
						if (ImNote.TrackN)
							NoteSet.insert(ImNote);
						break;
					}
				} 
			}
			PERKEYMAP.clear();
			printf("Key %d processed in sustains removing algorithm\n", key);
		}
	}
	void Load(std::wstring Link) {
		InitializeNPrepare(Link);
		printf("Notecount : Successfully pushed notes (Count) : Notes and tempo count without overlaps\n");
		CurrentTrack = 2;//fix//
		while (ReadSingleTrackFromCurPos()) {
			CurrentTrack++;
			std::cout << Notecount << " : " << PushedCount << " : " << OverlapslessCount << std::endl;
		}
		(*FileInput).close();

		if (dbg)printf("Magic finished with set size %d...\n", NoteSet.size());
		if (dbg && RemovingSustains) printf("Notecount might increase after remapping the MIDI\n");
		if (RemovingSustains) MapNotesAndReadBack();

		auto Y = NoteSet.begin();
		while (Y != NoteSet.end()) {
			if (TracksSet.find((*Y).TrackN) == TracksSet.end()) TracksSet.insert(((*Y).TrackN));
			Y++;
		}

		if (dbg)printf("Prepaired for output...\n");
		std::cout << "Tracks used: " << TracksSet.size() << std::endl;
		FormMIDI(Link);
	}
};

std::wstring OpenFileDialog(const wchar_t* Title) {
	OPENFILENAMEW ofn;       // common dialog box structure
	wchar_t szFile[1000];       // buffer for file name
	std::vector<std::wstring> InpLinks;
	ZeroMemory(&ofn, sizeof(ofn));
	ZeroMemory(szFile, 1000);
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrFile = szFile;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = L"MIDI Files(*.mid)\0*.mid\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.lpstrTitle = Title;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;
	if (GetOpenFileNameW(&ofn)) {
		return std::wstring(szFile);
	}
	else {
		return L"";
	}
}

int main(int argc, char** argv) {
	while(winapi_garbage::GetMode() < 0);
	VelocityMode = (winapi_garbage::RemovalModeLine == 2);
	RemovingSustains = (winapi_garbage::RemovalModeLine == 1);
	OverlapsRemover WRK;
	if(VelocityMode){
		if (argc <= 1) {
			while(winapi_garbage::GetTheshold() < 0);
			MinimalVolume = winapi_garbage::VelocityThreshold;
		}
		else
			MinimalVolume = std::stoi(std::string(argv[1]));
		printf("SAFOR. Art removing mode. Overlaps/sustains are not touched.\n");
	}
	else{
		if (RemovingSustains)
			printf("SAFSOR. Note remapping enabled. Velocity is not preserved.\n");
		else
			printf("SAFOR. Velocity Edition.\n");
	}
	std::cout << "\"Open file\" dialog should appear soon...\n";
	std::wstring filenames;
	while ((filenames = OpenFileDialog(L"Select MIDI File.")).empty());
	if (filenames.size()) {
		std::cout << "Filename in ASCII: ";
		for (auto& ch : filenames)
			std::cout << (char)ch;
		std::cout << std::endl;
		WRK.Load(filenames);
	}
	system("pause");
	return 0;
}
