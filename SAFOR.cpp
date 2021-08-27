#include <iostream>
#include <set>
#include <string>
#include <stack>
#include <vector>
#include <fstream>
#include <iterator>
#include <list>
#include <map>
#include <boost/container/flat_set.hpp>
#include <windows.h>
#include <thread>

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
//using namespace std;

#define VELOCITYTHRESHOLDFLAG
#define REMSUST false

#ifndef REMSUST
#define REMSUST true
#endif

constexpr bool RemovingSustains = REMSUST;
#pragma pack(push, 1)

#ifdef VELOCITYTHRESHOLDFLAG
BYTE MinimalVolume = 0;
#endif

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
	else if (!RemovingSustains && O.Vol > N.Vol) { //N.Vol=O.Vol;
		N.Vol = O.Vol;
	}
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
	//multiset<NoteObject,less<NoteObject>,moya_alloc::allocator<NoteObject,100000>> NoteSet;
	//multiset<TrackNumberType> TracksSet;//tracks
	//map<DWORD,boost::container::flat_multiset<PrepairedEvent>> MappedNotesSet;
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
			if (!ParseEvent(CurrentTick += RealTick))return 1;
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
#ifdef VELOCITYTHRESHOLDFLAG
		if (Ev.Vol < MinimalVolume && Ev.Key < 0xFF) return;
#else
		PushedCount++;
		auto e_pair = NoteSet.equal_range(Ev);
		if (NoteSet.size() && e_pair.first != NoteSet.end()) {
			auto current_p = e_pair.first;
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
#endif
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
				//if(dbg)printf("NOTEOFF:%x%x00 at %llu\n",RunningStatus,ByteVariable,absTick);
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
				//if(dbg)printf("NOTEON:%x%x%x at %llu\n",RunningStatus,ByteVariable,ByteVar2,absTick);
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
				//(*FileInput).seekg(vlv,std::ios::cur);
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

					//if(dbg)printf("TEMPO:%x at %x\n",vlv,(*FileInput).tellg());
					NoteObject Event;//event push prepairings
					Event.Key = 0xFF;
					Event.TrackN = 0;
					Event.Tick = absTick;
					Event.Len = vlv;
					PushNote(Event);
				}
				else {
					vlv = ReadVLV();
					//if(dbg)printf("REGMETASIZE:%x at %x\n",vlv,(*FileInput).tellg());
					//(*FileInput).seekg(vlv,std::ios::cur);
					for (int i = 0; i < vlv; i++)
						(*FileInput).get();
				}
			}
			else {
				if (RunningStatus >= 0x80 && RunningStatus <= 0x8F) { //NOTEOFF
					Notecount++;
					//RunningStatus=RunningStatus;
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
				 //RunningStatus=RunningStatus;
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
				 //RunningStatus=RunningStatus;
					(*FileInput).get();
				}
				else if (RunningStatus >= 0xC0 && RunningStatus <= 0xDF) {
					//RunningStatus=RunningStatus;
				}
				else {
					std::cout << "Imparseable data...\n\tdebug:" << (unsigned int)RunningStatus << ":" << (unsigned int)ByteVar1 << ":Off(FBegin):";
					printf("%x\n", (*FileInput).tellg());
					BYTE I = 0, II = 0, III = 0;
					while (!(I == 0x2F && II == 0xFF && III == 0) && !(*FileInput).eof()) {
						III = II;
						II = I;
						I = (*FileInput).get();
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
		std::cout << "Single pass scan has started... it might take a while...\n";
		auto Y = NoteSet.begin();
		UnsigedLongInt _Counter = 0;
		PrepairedEvent Event;
		NoteObject Note;//prev out, out
		while (Y != NoteSet.end()) {
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
				//if(dbg)printf("NOTE\filenames");
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
				//if(dbg)printf("F\n");
				_Counter++;
				if (_Counter % EDGE_LOGGER == 0)
					printf("Single pass scan: %u note\n", _Counter);
			}
			Y = NoteSet.erase(Y);
		}
		NoteSet.clear();
		std::cout << "Single pass scan has finished... Notecount: " << _Counter << std::endl;
	}
	void FormMIDI(std::wstring Link) {
		printf("Starting enhanced output algorithm\n");
		SinglePassMapFiller();
		std::vector<BYTE> Track;
		auto pfstr = open_wide_stream<std::ostream>((Link + (
#ifdef VELOCITYTHRESHOLDFLAG
			L".AR.mid"
#else
			(RemovingSustains) ? L".SOR.mid" : L".OR.mid"
#endif
			)), L"wb");
		std::ostream& fout = *pfstr;
		auto Y = MappedNotesSet.begin();
		btree::btree_multiset<PrepairedEvent>::iterator U;
		btree::btree_multiset<PrepairedEvent>* pMS;
		PrepairedEvent Event, PrevEvent;
		if (dbg)
			printf("Output..\n");
		//fout<<'M'<<'ByteVar2'<<'h'<<'d'<<(BYTE)0<<(BYTE)0<<(BYTE)0<<(BYTE)6<<(BYTE)0<<(BYTE)1;//header
		//fout<<(BYTE)((TracksSet.size()>>8))<<(BYTE)((TracksSet.size()&0xFF));//track number
		//fout<<(PPQN>>8)<<(PPQN&0xFF);//ppqn
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
				//if(dbg)printf("dtFormat... %d\n",tTick);
				do { //delta time formatiing begins here
					Track.push_back(tTick & 0x7F);
					tTick >>= 7;
					clen++;
				} while (tTick != 0);
				for (int i = 0; i < (clen >> 1); i++) {
					std::swap(Track[Track.size() - 1 - i], Track[Track.size() - clen + i]);
				}
				for (int i = 2; i <= clen; i++) {
					Track[Track.size() - i] |= 0x80;///hack (going from 2 instead of going from one)
				}
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
			//copy(Track.begin(),Track.end(),ostream_iterator<BYTE>(fout));
			if (dbg)printf("Track %u went to output\n", (*Y).first);
			Track.clear();
			Y++;
		}
		//fout.close();
	}
	void MapNotesAndReadBack() {
		std::vector<DWORD> PERKEYMAP;
		std::vector<TrackSymbol> KEYVEC;
		NoteObject ImNote;
		TrackSymbol VecInsertable;
		UnsigedLongInt T, size, LastEdge = 0;
		if (!NoteSet.size())return;
		auto Y = --NoteSet.end();
		for (int key = 0; key < 128; key++) {
			ImNote.Key = key;
			ImNote.Vol = 1;
			Y = NoteSet.begin();
			while (Y != NoteSet.end()) {
				if ((*Y).Key == key) {
					VecInsertable.Tick = (*Y).Tick;
					VecInsertable.TrackN = (*Y).TrackN;
					VecInsertable.Len = (*Y).Len;
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
			printf("Expected size: %u\n", (KEYVEC.back().Tick + KEYVEC.back().Len)); // hell
			if (KEYVEC.back().Tick + KEYVEC.back().Len >= PERKEYMAP.size()) {
				PERKEYMAP.resize(KEYVEC.back().Tick + KEYVEC.back().Len, 0);
				printf("Key map expansion %u\n", PERKEYMAP.size());
			}
			for (auto it = KEYVEC.begin(); it != KEYVEC.end(); ++it) {
				size = (*it).Tick + (*it).Len;
				if (size > PERKEYMAP.size()) {
					printf("Resize to %u from %u\n", size, PERKEYMAP.size());
					PERKEYMAP.resize(size, 0);
				}
				PERKEYMAP[(*it).Tick] = ((*it).TrackN << 1) | 1;
				for (UnsigedLongInt tick = (*it).Tick + 1; tick < size; tick++)
					PERKEYMAP[tick] = ((*it).TrackN << 1);
			}
			printf("Key map traversal ended\n");
			KEYVEC.clear();
			T = 0;
			LastEdge = 0;
			size = PERKEYMAP.size();
			while (T < size) {
				LastEdge = T;
				for (T++; T < size; T++) {
					if (PERKEYMAP[T] >> 1 != PERKEYMAP[T - 1] >> 1 || PERKEYMAP[T] & 1) {
						ImNote.Len = T - LastEdge;
						ImNote.Tick = LastEdge;
						ImNote.TrackN = (PERKEYMAP[T - 1] >> 1);
						LastEdge = T;
						if (ImNote.TrackN)NoteSet.insert(ImNote);
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
			//std::cout << (*Y).Tick << " " << (*Y).Len << " " << (*Y).TrackN << " " << (*Y).Key <<  std::endl;
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
	std::ofstream of;
	OverlapsRemover WRK;//yoi no?oeoo?a...
#ifdef VELOCITYTHRESHOLDFLAG
	if (argc <= 1) {
		printf("SAFOR. Art removing mod.\nEnter the minimal \"pass\" volume (0-127): ");
		std::cin >> MinimalVolume;
	}
	else
		MinimalVolume = std::stoi(std::string(argv[1]));
#else
	if (RemovingSustains)
		printf("SAFSOR. Remapping notes. Velocity is not preserved.\n");
	else
		printf("SAFOR. Velocity Edition.\n");
#endif
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
