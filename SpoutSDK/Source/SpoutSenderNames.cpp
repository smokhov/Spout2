/**

	spoutSenderNames.cpp
	Spout sender management

	Thanks and credit to Malcolm Bechard for modifications to this class

	https://github.com/mbechard	

	- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	25.04.14 - started class file
	27.05.14 - cleanup using memory map creation, open, close, lock
	05.06.14 - FindSenderName - allow for a null name entered
	08.06.14 - rebuild
	12.06.13 - major revision, included map handling
	23-07-14 - cleanup of DX9 / DX11 functions
			 - Changed CheckSender logic
	27.07-14 - changed mutex lock creation due to memory leak
	28-07-14 - major change
			 - remove handle management
			 - changed map creation and release
	30-07-14 - Map locks and cleanup
	31-07-14 - fixed duplicate names class object
	01-08-14 - fixed mutex handle leak / cleanup
	03-08-14 - fixed GetActiveSenderInfo
	-- names class revision additions --
	22-08-14 - activated event locks
	03.09.14 - cleanup
	10.10.14 - Restored CreateSender for use by DirectX apps
	01.08.15 - FindSender
				- return false if the the sender is not registered
				- if registered sender is no longer there release it
			 - CheckSender bug - Name for ReleaseSenderName was wrong
	24.08.15 - the active sender is the one selected or the last one opened by the user
			   so don't limit to the first sender
	15.09.15 - removed "using namespace std" from header
	24.02.16 - replaced #define MaxSenders with a global variable m_MaxSenders
			 - changed readSenderSetFromBuffer to create a buffer based on
			   the number of senders in the map passed
			 - changed writeBufferFromSenderSet to use the global m_MaxSenders
			 - Created SetMaxSenders to set m_MaxSenders

	- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	Copyright (c) 2014-2016, Lynn Jarvis. All rights reserved.

	Redistribution and use in source and binary forms, with or without modification, 
	are permitted provided that the following conditions are met:

		1. Redistributions of source code must retain the above copyright notice, 
		   this list of conditions and the following disclaimer.

		2. Redistributions in binary form must reproduce the above copyright notice, 
		   this list of conditions and the following disclaimer in the documentation 
		   and/or other materials provided with the distribution.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"	AND ANY 
	EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
	OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE	ARE DISCLAIMED. 
	IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
	INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
	PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
	LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
	OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
	- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

*/
#include "spoutSenderNames.h"
#include <assert.h>

spoutSenderNames::spoutSenderNames() {
	m_senders = new std::unordered_map<std::string, SpoutSharedMemory*>();
	m_MaxSenders = 10; // default maximum number of senders
}

spoutSenderNames::~spoutSenderNames() {

	for (auto itr = m_senders->begin(); itr != m_senders->end(); itr++)
	{
		delete itr->second;
	}
	delete m_senders;
	
}

//
// =========================
// multiple Sender functions
// ========================= 
//
// Register a new Sender by adding to the list of Sender names
//
bool spoutSenderNames::RegisterSenderName(const char* Sendername) {

	std::pair<std::set<std::string>::iterator, bool> ret;
	std::set<std::string> SenderNames; // set of names

	// Create the shared memory for the sender name set if it does not exist
	if(!CreateSenderSet())	return false;

	char *pBuf = m_senderNames.Lock();
	if (!pBuf) return false;

	// Register the sender name in the list of spout senders
	readSenderSetFromBuffer(pBuf, SenderNames, m_MaxSenders);

	//
	// Add the Sender name to the set of names
	//
	ret = SenderNames.insert(Sendername);
	if(!ret.second) {
		// See if there are any dangling entries that aren't valid anymore
		cleanSenderSet();
		readSenderSetFromBuffer(pBuf, SenderNames, m_MaxSenders);
		ret = SenderNames.insert(Sendername);
	}

	if(ret.second) {
		// write the new map to shared memory
		writeBufferFromSenderSet(SenderNames, pBuf, m_MaxSenders);
		// Set as the active Sender if it is the first one registered
		// Thereafter the user can select an active Sender using SpoutPanel or SpoutSenders
		m_activeSender.Create("ActiveSenderName", SpoutMaxSenderNameLen);
		// The active sender is the one selected by the user
		// or the last one opened by the user, so don't limit to the first sender
		// Set the current sender name as active
		SetActiveSender(Sendername);  
	}

	m_senderNames.Unlock();

	return ret.second;
}

//
// Removes a Sender from the set of Sender names
//
bool spoutSenderNames::ReleaseSenderName(const char* Sendername) 
{
	std::set<std::string> SenderNames;
	std::string namestring;
	char name[SpoutMaxSenderNameLen];

	// Create the shared memory for the sender name set if it does not exist
	if(!CreateSenderSet())	return false;

	char *pBuf = m_senderNames.Lock();
	if (!pBuf) return false;

	namestring = Sendername;
	auto foundSender = m_senders->find(namestring);
	if (foundSender != m_senders->end()) {
		delete foundSender->second;
		m_senders->erase(namestring);
	}

	readSenderSetFromBuffer(pBuf, SenderNames, m_MaxSenders);

	// Discovered that the project properties had been set to CLI
	// Properties -> General -> Common Language Runtime Support
	// and this caused the set "find" function not to work.
	// It also disabled intellisense.

	// Get the current map to update the list
	if(SenderNames.find(Sendername) != SenderNames.end() ) {

		SenderNames.erase(Sendername); // erase the matching Sender

		writeBufferFromSenderSet(SenderNames, pBuf, m_MaxSenders);

		// Is there a set left ?
		if(SenderNames.size() > 0) {
			// This should be OK because the user selects the active sender
			// Was it the active sender ?
			if( (getActiveSenderName(name) && strcmp(name, Sendername) == 0) || SenderNames.size() == 1) { 
				// It was, so choose the first in the list
				std::set<std::string>::iterator iter = SenderNames.begin();
				namestring = *iter;
				strcpy_s(name, namestring.c_str());
				// Set it as the active sender
				setActiveSenderName(name);
			}
		}
		m_senderNames.Unlock();
		return true;
	}

	m_senderNames.Unlock();
	return false; // Sender name not in the set or no set in shared mempry

} // end RemoveSender



// Test to see if the Sender name exists in the sender set
bool spoutSenderNames::FindSenderName(const char* Sendername)
{
	std::string namestring;
	std::set<std::string> SenderNames;
	
	if(Sendername[0]) { // was a valid name passed
		// Get the current list to update the passed list
		if(GetSenderSet(SenderNames)) {
			// Does the name exist
			if(SenderNames.find(Sendername) != SenderNames.end() ) {
				return true;
			}
		}
	}

	return false;
}

void spoutSenderNames::cleanSenderSet()
{
	if(!CreateSenderSet()) {
		return;
	}

	char *pBuf = m_senderNames.Lock();

	if (!pBuf) {
	    return;
	}

	std::set<std::string> SenderNames;
	readSenderSetFromBuffer(pBuf, SenderNames, m_MaxSenders);

	bool changed = false;

	for (auto itr = SenderNames.begin(); itr != SenderNames.end(); )
	{
		// It's one of ours, so thats fine
		if (m_senders->find(*itr) != m_senders->end())
		{
			itr++;
			continue;
		}
		SpoutSharedMemory mem;
		// This isn't found, we clean it up
		if (!mem.Open((*itr).c_str()))
		{
			changed = true;
			SenderNames.erase(itr++);
		}
		else
		{
			++itr;
		}
		
	}

	if (changed)
	{
		writeBufferFromSenderSet(SenderNames, pBuf, m_MaxSenders);
	}

	m_senderNames.Unlock();
	
}


// Function to return the set of Sender names in shared memory.
bool spoutSenderNames::GetSenderNames(std::set<std::string> *Sendernames)
{
	// Get the current list to update the passed list
	if (GetSenderSet(*Sendernames)) {
		return true;
	}

	return false;
}


int spoutSenderNames::GetSenderCount() {

	std::set<std::string> SenderSet;
	std::set<std::string>::iterator iter;
	std::string namestring;
	char name[SpoutMaxSenderNameLen];
	SharedTextureInfo info;

	// Create the shared memory for the sender name set if it does not exist
	if(!CreateSenderSet()) {
		return 0;
	}

	// Doing multiple operations on the sender list, keep it locked
	if (!m_senderNames.Lock())
	{
		return 0;
	}

	// get the name list in shared memory into a local list
	GetSenderNames(&SenderSet);

	// Now we have a local set of names
	// 27.12.13 - noted that if a Processing sketch is stopped by closing the window
	// all is OK and either the "stop" or "dispose" overrides work, but if STOP is used, 
	// or the sketch is closed, neither the exit or dispose functions are called and
	// the sketch does not release the sender.
	// So here we run through again and check whether the sender exists and if it does not
	// release the sender from the local sender list
	if(SenderSet.size() > 0) {
		for(iter = SenderSet.begin(); iter != SenderSet.end(); iter++) {
			namestring = *iter; // the Sender name string
			strcpy_s(name, namestring.c_str());
			// we have the name already, so look for it's info
			if(!getSharedInfo(name, &info)) {
				// Sender does not exist any more
				ReleaseSenderName(name); // release from the shared memory list
			}
		}
	}

	// Get the new set back
	if(GetSenderNames(&SenderSet)) {
		m_senderNames.Unlock();
		return((int)SenderSet.size());
	}

	m_senderNames.Unlock();

	return 0;
}


// Get sender info given a sender index and knowing the sender count
// index                        - in
// sendername                   - out
// sendernameMaxSize            - in
// width, height, dxShareHandle - out
bool spoutSenderNames::GetSenderNameInfo(int index, char* sendername, int sendernameMaxSize, unsigned int &width, unsigned int &height, HANDLE &dxShareHandle)
{
	char name[SpoutMaxSenderNameLen];
	std::set<std::string> SenderNameSet;
	std::set<std::string>::iterator iter;
	std::string namestring;
	int i;
	DWORD format;

	if(GetSenderNames(&SenderNameSet)) {
		if(SenderNameSet.size() < (unsigned int)index)
			return false;

		i = 0;
		for(iter = SenderNameSet.begin(); iter != SenderNameSet.end(); iter++) {
			namestring = *iter; // the name string
			strcpy_s(name, namestring.c_str()); // the 256 byte name char array
			if(i == index) {
				strcpy_s(sendername, sendernameMaxSize, name); // the passed name char array
				break;
			}
			i++;
		}
		
		// Does the retrieved sender exist or has it crashed?
		// Find out by getting the sender info and returning it
		if(GetSenderInfo(sendername, width, height, dxShareHandle, format))
			return true;

	}

	return false;

} // end GetSenderNameInfo


//
// Maximum sender functions for development testing only
//

// Set the maximum number of senders contained in the sender map
// Subsequently a new sender map will be created large enough for the number of senders
// but if a map is already open, it's size will not be changed
void spoutSenderNames::SetMaxSenders(int maxSenders)
{
	// printf("spoutSenderNames - Setting max senders to %d\n", maxSenders);
	m_MaxSenders = maxSenders;
}


int spoutSenderNames::GetMaxSenders()
{
	// printf("Getting max senders %d\n", m_MaxSenders);
	return m_MaxSenders;
}


// This retrieves the info from the requested sender and fails if the sender does not exist
// For external access to getSharedInfo - redundancy
bool spoutSenderNames::GetSenderInfo(const char* sendername, unsigned int &width, unsigned int &height, HANDLE &dxShareHandle, DWORD &dwFormat)
{
	SharedTextureInfo info;

	if(getSharedInfo(sendername, &info)) {
		width		  = (unsigned int)info.width;
		height		  = (unsigned int)info.height;
		dxShareHandle = (HANDLE)info.shareHandle;
		dwFormat      = info.format;
		return true;
	}
	return false;
}


//
// Set texture info to a sender shared memory map without affecting the 
// interop class globals used for GL/DX interop texture sharing
// TODO - use pointer from initial map creation
bool spoutSenderNames::SetSenderInfo(const char* sendername, unsigned int width, unsigned int height, HANDLE dxShareHandle, DWORD dwFormat) 
{
	SharedTextureInfo info;

	std::string nameString = sendername;
	
	auto foundSender = m_senders->find(nameString);

	if (foundSender == m_senders->end())
	{
		return false;
	}

	auto senderInfoMap = foundSender->second;

	char *pBuf = senderInfoMap->Lock();

	if (!pBuf)
	{
		return false;
	}
	
	info.width			= (unsigned __int32)width;
	info.height			= (unsigned __int32)height;
	info.shareHandle	= (unsigned __int32)dxShareHandle; 
	info.format			= (unsigned __int32)dwFormat;
	// Usage not used

	memcpy((void *)pBuf, (void *)&info, sizeof(SharedTextureInfo) );

	senderInfoMap->Unlock();
	
	return true;

} // end SetSenderInfo



// Functions to set or get the active Sender name
// The "active" Sender is the one of the multiple Senders
// that is top of the list or is the one selected by the user from this list. 
// This active Sender information is saved in a separated shared
// memory from other Senders, identified by the name "ActiveSenderName"
// so it can be recalled at any time by clients if the user
// has selected a required Sender from a dialog or executable.
// The dialog or executable sets the info of the selected Sender
// into the ActiveSender shared memory so the clients can picks it up.
//  !!! The active Sender has to be a member of the Sender list !!!
bool spoutSenderNames::SetActiveSender(const char *Sendername)
{
	std::set<std::string> SenderNames;

	if (!CreateSenderSet())	{
		return false;
	}

	// Keep the sender set locked for this entire operation
	if (!m_senderNames.Lock())
	{
		return false;
	}

	// Get the current list to check whether the passed name is in it
	if(GetSenderSet(SenderNames)) {
		if(SenderNames.find(Sendername) != SenderNames.end() ) {
			if(setActiveSenderName(Sendername)) { // set the active Sender name to shared memory
				m_senderNames.Unlock();
				return true;
			}
		}
	}
	m_senderNames.Unlock();
	return false;

} // end SetActiveSender


// Function for clients to retrieve the current active Sender name
bool spoutSenderNames::GetActiveSender(char Sendername[SpoutMaxSenderNameLen])
{
	char ActiveSender[SpoutMaxSenderNameLen];
	SharedTextureInfo info;

	if(getActiveSenderName(ActiveSender)) {
		// Does it still exist ?
		if(getSharedInfo(ActiveSender, &info)) {
			strcpy_s(Sendername, SpoutMaxSenderNameLen, ActiveSender);
			return true;
		}
		else {
			// Erase the active sender map ?
		}
	}
	
	return false;

} // end GetActiveSender



// Function for clients to get the shared info of the active Sender
bool spoutSenderNames::GetActiveSenderInfo(SharedTextureInfo* info)
{
	char sendername[SpoutMaxSenderNameLen];

	// See if the shared memory of the active Sender exists
	if(GetActiveSender(sendername)) {
		if(getSharedInfo(sendername, info)) {
			return true;
		}
	}
	// It should exist because it is set whenever a Sender registers
	return false;
} // end GetActiveSenderInfo



//
// Retrieve the texture info of the active sender
// - redundancy 
bool spoutSenderNames::FindActiveSender(char sendername[SpoutMaxSenderNameLen], unsigned int &theWidth, unsigned int &theHeight, HANDLE &hSharehandle, DWORD &dwFormat)
{
    SharedTextureInfo TextureInfo;
	char sname[SpoutMaxSenderNameLen];

    if(GetActiveSender(sname)) { // there is an active sender
		if(getSharedInfo(sname, &TextureInfo)) {
			strcpy_s(sendername, SpoutMaxSenderNameLen, sname); // pass back sender name
			theWidth        = (unsigned int)TextureInfo.width;
			theHeight       = (unsigned int)TextureInfo.height;
			hSharehandle	= (HANDLE)TextureInfo.shareHandle;
			dwFormat        = (DWORD)TextureInfo.format;
			return true;
		}
	}

    return false;

} // end FindActiveSender


/////////////////////////////////////////////////////////////////////////////////////
// Functions to Create, Update and Close a sender and retrieve sender texture info //
// without initializing DirectX or the GL/DX interop functions                     //
/////////////////////////////////////////////////////////////////////////////////////

// ---------------------------------------------------------
//	Create a sender
// ---------------------------------------------------------
bool spoutSenderNames::CreateSender(const char *sendername, unsigned int width, unsigned int height, HANDLE hSharehandle, DWORD dwFormat)
{
	// Register the sender name
	// The function is ignored if the sender already exists
	RegisterSenderName(sendername);

	// Save the texture info for this sender
	if(!UpdateSender(sendername, width, height, hSharehandle, dwFormat))
		return false;

	return true;
		
} // end CreateSender


// ---------------------------------------------------------
//	Update the texture info of a sender
//	Used for example when a sender's texture changes size
// ---------------------------------------------------------
bool spoutSenderNames::UpdateSender(const char *sendername, unsigned int width, unsigned int height, HANDLE hSharehandle, DWORD dwFormat)
{
	std::string namestring = sendername;

	if (m_senders->find(namestring) == m_senders->end()) {
		// Create or open a shared memory map for this sender - allocate enough for the texture info
		SpoutSharedMemory *senderInfoMem = new SpoutSharedMemory();
		SpoutCreateResult result = senderInfoMem->Create(sendername, sizeof(SharedTextureInfo));
		if(result == SPOUT_CREATE_FAILED) {
			delete senderInfoMem;
			m_senderNames.Unlock();
			return false;
		}
		(*m_senders)[namestring] = senderInfoMem;
	}

	// Save the info for this sender in the sender shared memory map
	if(!SetSenderInfo(sendername, width, height, hSharehandle, dwFormat)) {
		return false;
	}


	return true;
		
} // end UpdateSender


// ===============================================================================
//	Functions to retrieve information about the shared texture of a sender
//
//	Possible detection by the caller of DX9 or DX11 sender from the Format field
//	Format is always fixed as D3DFMT_A8R8G8B8 for a DirectX9 sender and Format is set to 0
//	For a DirectX11 sender, the format field is set to the DXGI_FORMAT texture format 
//	Usage is fixed :
//		DX9  - D3DUSAGE_RENDERTARGET
//		DX11 - D3D11_USAGE_DEFAULT 
// ===============================================================================

// Find a sender and return the name, width and height, sharhandle and format
bool spoutSenderNames::FindSender(char *sendername, unsigned int &width, unsigned int &height, HANDLE &hSharehandle, DWORD &dwFormat)
{
	SharedTextureInfo info;

	// ---------------------------------------------------------
	//	For a receiver check the user entered Sender name, if one, to see if it exists
	if(sendername[0] == 0) {
		// Passed name was null, so find the active sender
		if(!GetActiveSender(sendername)) {
			return false;
		}
	}
	// now we have either an existing sender name or the active sender name

	// 01.08.15 - Is the given sender registered ?
	if(!FindSenderName(sendername))
		return false;

	// Sender is registered so try to get the sender information
	if(getSharedInfo(sendername, &info)) {
		width			= (unsigned int)info.width; // pass back sender size
		height			= (unsigned int)info.height;
		hSharehandle	= (HANDLE)info.shareHandle;
		dwFormat		= (DWORD)info.format;
		return true;
	}

	// 01.08.15 - Registered sender is no longer there so release it
	ReleaseSenderName(sendername);

	return false;

} // end FindSender


//
//	Check the details of an existing sender
//
//	1) Find the sender
//	2) Get it's texture info
//	3) Return the sharehandle, width, height, and format
//
//	Returns :
//		true - all OK.
//		  width and height are returned changed for sender size change
//		false - sender not found or size changed
//		  width and height are returned zero for sender not found
//
bool spoutSenderNames::CheckSender(const char *sendername, unsigned int &theWidth, unsigned int &theHeight, HANDLE &hSharehandle, DWORD &dwFormat)
{
	SharedTextureInfo info;

	// Is the given sender registered ?
	if(FindSenderName(sendername)) {
		// Does it still exist ?
		if(getSharedInfo(sendername, &info)) {
			// Return the texture info
			theWidth		= (unsigned int)info.width;
			theHeight		= (unsigned int)info.height;
			hSharehandle	= (HANDLE)info.shareHandle;
			dwFormat		= (DWORD)info.format;
			return true;
		}
		else {
			// Sender is registered but does not exist so close it
			ReleaseSenderName(sendername);
		}

	}

	// Return zero width and height to indicate sender not found
	theHeight = 0;
	theWidth  = 0;

	return false;

} // end CheckSender
// ================================================


///////////////////////////////////////////////////
// Private functions for multiple Sender support //
///////////////////////////////////////////////////

void spoutSenderNames::readSenderSetFromBuffer(const char* buffer, std::set<std::string>& SenderNames, int maxSenders)
{
	// first empty the set
	if(SenderNames.size() > 0) {
		SenderNames.erase (SenderNames.begin(), SenderNames.end() );
	}

	const char *buf = buffer;
	char name[SpoutMaxSenderNameLen];		// char array to test for nulls
	int i = 0;
	do {
		// the actual string retrieved from shared memory should terminate
		// with a null within the 256 chars.
		// At the end of the map there will be a null in the data.
		// Must use a character array to ensure testing for null.
		strncpy_s(name, buf, SpoutMaxSenderNameLen);
		if(name[0] > 0) {
			// insert name into set
			// seems OK with a char array instead of converting to a string first
			SenderNames.insert(name);
		}
		// increment by 256 bytes for the next name
		buf += SpoutMaxSenderNameLen;
		i++;
	} while (name[0] > 0 && i < maxSenders); // maxSenders has to be passed because this function is static

}


void spoutSenderNames::writeBufferFromSenderSet(const std::set<std::string>& SenderNames, char* buffer, int maxSenders)
{
	std::string namestring;
	char *buf = buffer; // pointer within the buffer
	int i = 0;
	std::set<std::string>::iterator iter;

	for(iter = SenderNames.begin(); iter != SenderNames.end(); iter++) {
		namestring = *iter; // the string to copy to the buffer
		// copy it with 256 max length although only the string length will be copied
		strcpy_s(buf, SpoutMaxSenderNameLen, namestring.c_str());
		// move the buffer pointer on for the next Sender name
		buf += SpoutMaxSenderNameLen;
		i++;
		if(i > maxSenders) break; // do not exceed the size of the local buffer
	}

	// If we haven't totally filled the sender list, 
	// then null terminate the next entry to terminate the list
	if (i < maxSenders) {
		*buf = '\0';
	}
}

//
//  Functions to read and write the list of Sender names to/from shared memory
//

// Create a shared memory map and copy the Sender names set to shared memory
bool spoutSenderNames::CreateSenderSet() 
{

	// Set up Shared Memory for all the sender names

	// The map will be created using m_MaxSenders unless a map already exists
	// in which case the map size will be the same as wen it was created.
	// If it was created by a 2.004 app this will have a maximum of 10 senders.

	SpoutCreateResult result = m_senderNames.Create("SpoutSenderNames", m_MaxSenders*SpoutMaxSenderNameLen);
	if(result == SPOUT_CREATE_FAILED) {
		return false;
	}
	
	return true;

} // end CreateSenderSet


bool spoutSenderNames::GetSenderSet(std::set<std::string>& SenderNames) {

	std::set<std::string>::iterator iter;
	char* pBuf = NULL;

	// Open or create m_sendernames
	if (!CreateSenderSet())	{
		return false;
	}

	pBuf = m_senderNames.Lock();
	if (!pBuf) {
		return false;
	}

	// The data has been stored with 256 bytes reserved for each Sender name
	// and nothing will have changed with the map yet
	if(pBuf[0] == 0) { // no senders yet
		m_senderNames.Unlock();
		return true;
	}

	// Read back from the mapped memory buffer and rebuild the set that was passed in
	// The set will then contain the senders currently in the memory map
	// and allow for any that have been added or deleted
	readSenderSetFromBuffer(pBuf, SenderNames, m_MaxSenders);

	m_senderNames.Unlock();

	return true;

} // end GetSenderSet


// Create a shared memory map to set the active Sender name to shared memory
// This is a separate small shared memory with a fixed sharing name
// that clients can use to retrieve the current active Sender
bool spoutSenderNames::setActiveSenderName(const char* SenderName) 
{
	int len = (int)strlen(SenderName);
	if(len  == 0 || len + 1 > SpoutMaxSenderNameLen)	return false;

	m_activeSender.Create("ActiveSenderName", SpoutMaxSenderNameLen);

	char *pBuf = m_activeSender.Lock();

	if(!pBuf) {
		return false;
	}

	// Fill it with the Sender name string
	memcpy( (void *)pBuf, (void *)SenderName, len + 1 ); // write the Sender name string to the shared memory
	
	m_activeSender.Unlock();

	return true;

} // end setActiveSenderName



// Get the active Sender name from shared memory
bool spoutSenderNames::getActiveSenderName(char SenderName[SpoutMaxSenderNameLen]) 
{
	bool result = m_activeSender.Open("ActiveSenderName");
	if (!result)
		return false;

	char *pBuf = m_activeSender.Lock();

	// Open the named memory map for the active sender and return a pointer to the memory
	if(!pBuf) {
		return false;
	}

	memcpy(SenderName, (void *)pBuf, SpoutMaxSenderNameLen ); // get the name string from shared memory
	
	m_activeSender.Unlock();

	return true;

} // end getActiveSenderName



// Return current sharing handle, width and height of a Sender
// A receiver checks this all the time so it has to be compact
// Does not have to be the info of this instance
// so the creation pointer and handle may not be known
bool spoutSenderNames::getSharedInfo(const char* sharedMemoryName, SharedTextureInfo* info) 
{
	SpoutSharedMemory mem;

	// Possibly faster because the functon is called all the time
	if(mem.Open(sharedMemoryName)) {
		char *pBuf = mem.Lock();
		if(pBuf) {
			memcpy((void *)info, (void *)pBuf, sizeof(SharedTextureInfo) );
			mem.Unlock();
			return true;
		}
	}

	return false;

} // end getSharedInfo


// 12.06.15 - Added to allow direct modification of a sender's information in shared memory
bool spoutSenderNames::setSharedInfo(const char* sharedMemoryName, SharedTextureInfo* info) 
{
	SpoutSharedMemory mem;
	bool result = mem.Open(sharedMemoryName);

	if (!result) {
		return false;
	}

	char *pBuf = mem.Lock();

	if (!pBuf)	{
		return false;
	}

	memcpy((void *)pBuf, (void *)info, sizeof(SharedTextureInfo) );

	mem.Unlock();
	
	return true;

} // end getSharedInfo


//---------------------------------------------------------
bool spoutSenderNames::SenderDebug(const char *Sendername, int size)
{
	// HANDLE hMap1 = NULL;
	// HANDLE hMap2 = NULL;
	// HANDLE hMap3 = NULL;
	std::set<std::string> SenderNames;
	std::set<std::string>::iterator iter;
	std::string namestring;

	UNREFERENCED_PARAMETER(Sendername);
	UNREFERENCED_PARAMETER(size);

	printf("**** SENDER DEBUG ****\n");

	m_senderNames.Debug();

	// Check the sender names
	/*
	// printf("    GetSenderSet\n");
	if(GetSenderSet(SenderNames)) {
		// printf("        SenderNames size = [%d]\n", SenderNames.size());
		if (SenderNames.size() > 0) {
			for(iter = SenderNames.begin(); iter != SenderNames.end(); iter++) {
				namestring = *iter;
				// printf("            Sender : [%s]\n", namestring.c_str());
			}
		}
	}
	else {
		// printf("    GetSenderSet failed\n");
	}
	*/

	printf("    GetSenderNames\n");
	if(GetSenderNames(&SenderNames)) {
		printf("        SenderNames size = [%d]\n", SenderNames.size());
		if (SenderNames.size() > 0) {
			for(iter = SenderNames.begin(); iter != SenderNames.end(); iter++) {
				namestring = *iter;
				printf("            Sender : [%s]\n", namestring.c_str());
			}
		}
		else {
			printf("    SenderNames size = 0\n");
		}
	}
	else {
		printf("    GetSenderSet failed\n");
	}

	/*
	// printf("2) Closing - hSenderNamesMap = [%x], pSenderNamesMap = [%x]\n", m_hSenderNamesMap, m_pSenderNamesMap);

	// Close and try to reopen
	CloseMap(m_pSenderNamesMap, m_hSenderNamesMap);

	hMap2 = OpenFileMappingA (FILE_MAP_ALL_ACCESS, FALSE, "SpoutSenderNames");
	if(hMap2) {
		// printf("    Sendernames map [%x] did not close\n", hMap2);
		CloseHandle(hMap2);
	}
	else {
		// printf("    Closed sendernames map OK\n");
	}

	CloseMap(m_pActiveSenderMap, m_hActiveSenderMap);
	*/

	m_activeSender.Debug();

	return true;
}
