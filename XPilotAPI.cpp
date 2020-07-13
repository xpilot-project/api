/*
 * Original work Copyright (c) 2019, Birger Hoppe
 * Modified work Copyright (c) 2020, Justin Shannon
 *
 * Parts of this project have been copied from LTAPI and is copyrighted
 * by Birger Hoppe under the terms of the MIT license.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <cstring>
#include <algorithm>
#include <cassert>

#include "XPilotAPI.h"

#include <XPLMPlugin.h>

#ifdef min
#undef min
#endif

#define XPILOT_PLUGIN_SIGNATURE "org.vatsim.xpilot"

 /// @brief Defines static object to access dataRef and fetches its value into `var`
 /// @param var Variable receiving current dataRef's value
 /// @param drName Name to be used for static variable, prepended with "DR"
 /// @param dataRef Name of dataRef as C string
 /// @param type Type of dataRef like Int, Float, Byte
#define ASSIGN_DR_NAME(var,drName,dataRef,type)                 \
static LTDataRef DR##drName(dataRef);                           \
var = DR##drName.get##type();

/// @brief Defines static object `XPilotDataRef` to access dataRef and `return`s its current value
/// @param dataRef Name of dataRef as C string
/// @param type Type of dataRef like Int, Float, Byte
#define RETURN_DR(dataRef,type)                                 \
static XPilotDataRef DR(dataRef);                               \
return DR.get##type();

/// Set last element of array = `0`, meant to ensure zero-termination of C strings
#define ZERO_TERM(str) str[sizeof(str)-1] = 0

namespace XPilotAPI {
    /// @brief Inverse for gmtime, i.e. converts `struct tm` to `time_t` in ZULU timezone
    /// @param _Tm Date/time structure to convert
    /// @return Same Date/time, converted to `time_t`in ZULU timezone
    time_t timegm(struct tm* _Tm)
    {
        time_t t = mktime(_Tm);
        return t + (mktime(localtime(&t)) - mktime(gmtime(&t)));
    }

    /// @brief Fairly fast conversion to hex string.
    /// @param n The number to convert
    /// @param minChars (optional, defaults to 6) minimum number of hex digits, pre-filled with `0
    /// @return Upper-case hex string with at least `minDigits` characters
    ///
    /// Idea is taken from `std::to_chars` implementation available with C++ 17
    std::string hexStr(uint64_t n, unsigned minChars = 6)
    {
        char buf[11] = { 0,0,0,0,0,0,0,0,0,0,0 };
        char* last = buf + sizeof(buf) - 2;       // we keep one last zero for zero-termination!
        while (last != buf)
        {
            auto c = n % 16;                    // digit to convert
            *--last = "0123456789ABCDEF"[c];    // sets the digit and decrements the pointer
            n /= 16;                            // remainder for next cycle
            if (n == 0)                         // nothing left -> done
            {
                // some more leading zeroes needed?
                if (minChars > sizeof(buf) - 1)
                    minChars = sizeof(buf) - 1;
                while (buf + sizeof(buf) - 1 - minChars < last)
                    *--last = '0';
                return last;
            }
        }
        return "-OVFL-";                        // overflow
    }
}

XPilotAPIAircraft::XPilotAPIAircraft()
{}

XPilotAPIAircraft::~XPilotAPIAircraft()
{}

/// Puts together a string if at max 3 compontens:
/// 1. an identifier (flight number, call sign, key)
/// 2. a/c type (model ICAO, model human readble)
/// 3. origin/destination
/// @return Description of aircraft useful as label
std::string XPilotAPIAircraft::getDescription() const
{
    std::string ret;

    // 1. identifier
    if (info.callSign[0])
        ret = info.callSign;
    else
        ret = key;

    // 2. a/c type
    if (info.modelIcao[0]) {
        ret += " (";
        ret += info.modelIcao;
        ret += ')';
    }

    // 3. origin/destination
    if (info.origin[0] || info.destination[0]) {
        ret += " ";
        ret += info.origin[0] ? info.origin : "?";
        ret += "-";
        ret += info.destination[0] ? info.destination : "?";
    }

    return ret;
}

/// Copies the provided `bulk` data and sets `bUpdated` to `true`
/// if the provided data matches this aircraft.
/// @note This function can _set_ this object's `key` for the first and only time.
bool XPilotAPIAircraft::updateAircraft(const XPilotAPIBulkData& __bulk, size_t __inSize)
{
    // first time init of this LTAPIAircraft object?
    if (key.empty()) {
        // yes, so we accept the offered aircraft as ours now:
        keyNum = (unsigned)__bulk.keyNum;
        key = XPilotAPI::hexStr(__bulk.keyNum);
    }
    else {
        // our key isn't empty, so we continue only if the aircraft offered
        // is the same!
        if (__bulk.keyNum != keyNum)
            return false;
    }

    // just copy the data
    bulk = __bulk;

    // has been updated
    bUpdated = true;
    return true;
}

/// Copies the provided `info` data and sets `bUpdated` to `true`
/// if the provided data matches this aircraft.
/// @note This function will never overwrite `key`!
///       A new LTAPIAircraft object will always receive a call to
///       the above version (with `LTAPIBulkData`) first before receiving
///       a call to this version (with `LTAPIBulkInfoTexts`).
bool XPilotAPIAircraft::updateAircraft(const XPilotAPIBulkInfoTexts& __info, size_t __inSize)
{
    // We continue only if the aircraft offered
    // is the same as we represent!
    if (__info.keyNum != keyNum)
        return false;

    // just copy the data
    info = __info;

    // We don't trust nobody, so we make sure that the C strings are zero-terminated
    ZERO_TERM(info.modelIcao);
    ZERO_TERM(info.opIcao);
    ZERO_TERM(info.callSign);
    ZERO_TERM(info.squawk);
    ZERO_TERM(info.origin);
    ZERO_TERM(info.destination);
    ZERO_TERM(info.cslModel);

    // has been updated
    bUpdated = true;
    return true;
}

XPilotAPIConnect::XPilotAPIConnect(fCreateAcObject* _pfCreateAcObject, int numBulkAc) :
    // clamp numBulkAc between 1 and 100
    iBulkAc(numBulkAc < 1 ? 1 : numBulkAc > 100 ? 100 : numBulkAc),
    // reserve memory for bulk data transfer from LiveTraffic
    vBulkNum(new XPilotAPIAircraft::XPilotAPIBulkData[iBulkAc]),
    vInfoTexts(new XPilotAPIAircraft::XPilotAPIBulkInfoTexts[iBulkAc]),
    pfCreateAcObject(_pfCreateAcObject)
{}

bool XPilotAPIConnect::doesXPilotControlAI()
{
    RETURN_DR("xpilot/ai_controlled", Bool);
}

int XPilotAPIConnect::getXPilotNumAc()
{
    RETURN_DR("xpilot/num_aircraft", Int);
}

const MapXPilotAPIAircraft& XPilotAPIConnect::UpdateAcList(MapXPilotAPIAircraft* plistRemovedAc)
{
    // These are the bulk input/output dataRefs in LiveTraffic,
    // with which we fetch mass data from LiveTraffic
    static XPilotDataRef DRquick("xpilot/bulk/quick");
    static XPilotDataRef DRexpsv("xpilot/bulk/expensive");

    // a few sanity checks...without LT displaying aircrafts
    // and access to ac/key there is nothing to do.
    // (Calling doesLTDisplayAc before calling any other dataRef
    //  makes sure we only try accessing dataRefs when they are available.)
    const int numAc = isXPilotAvail() && doesXPilotDisplayAc() && DRquick.isValid() && DRexpsv.isValid() ? getXPilotNumAc() : 0;
    if (numAc <= 0) {
        // does caller want to know about removed aircrafts?
        if (plistRemovedAc)
            for (MapXPilotAPIAircraft::value_type& p : mapAc)
                // move all objects over to the caller's list's end
                plistRemovedAc->emplace_back(std::move(p.second));
        // clear our map
        mapAc.clear();
        return mapAc;
    }

    // *** There are numAc aircrafts to be reported ***

    // To figure out which aircraft has gone we keep an update flag
    // with the aircraft. Let's reset that flag first.
    for (MapXPilotAPIAircraft::value_type& p : mapAc)
        p.second->resetUpdated();

    // *** Read bulk info from LiveTraffic ***

    // Always do the rather fast call for numeric data
    int sizeLTStruct = 0;                     // not yet used, will become important once different versions exist
    if (DoBulkFetch<XPilotAPIAircraft::XPilotAPIBulkData>(numAc, DRquick, sizeLTStruct,
        vBulkNum) ||
        // do the expensive call for textual data if the above one added new objects, OR
        // if 3 seconds have passed since the last call
        std::chrono::steady_clock::now() - lastExpsvFetch > sPeriodExpsv)
    {
        // expensive call for textual data
        sizeLTStruct = 0;
        DoBulkFetch<XPilotAPIAircraft::XPilotAPIBulkInfoTexts>(numAc, DRexpsv, sizeLTStruct,
            vInfoTexts);
        lastExpsvFetch = std::chrono::steady_clock::now();
    }

    // ***  Now handle aircrafts in our map, which did _not_ get updated ***
    for (MapXPilotAPIAircraft::iterator iter = mapAc.begin();
        iter != mapAc.end();
        /* no loop increment*/)
    {
        // not updated?
        if (!iter->second->isUpdated()) {
            // Does caller want to take over them?
            if (plistRemovedAc)
                // here you go...your object now
                plistRemovedAc->emplace_back(std::move(iter->second));
            // in any case: remove from our map and increment to next element
            iter = mapAc.erase(iter);
        }
        else
            // go to next element (without removing this one)
            iter++;
    }

    // We're done, return the result
    return mapAc;
}

// Finds an aircraft for a given multiplayer slot
SPtrXPilotAPIAircraft XPilotAPIConnect::getAcByMultIdx(int multiIdx) const
{
    // sanity check: Don't search for 0...there are too many of them
    if (multiIdx < 1)
        return SPtrXPilotAPIAircraft();

    // search the map for a matching aircraft
    MapXPilotAPIAircraft::const_iterator iter =
        std::find_if(mapAc.cbegin(), mapAc.cend(),
            [multiIdx](const MapXPilotAPIAircraft::value_type& pair)
    { return pair.second->getMultiIdx() == multiIdx; });

    // return a copy of they pointer if found
    return iter == mapAc.cend() ? SPtrXPilotAPIAircraft() : iter->second;
}

// fetch bulk data and create/update aircraft objects
template <class T>
bool XPilotAPIConnect::DoBulkFetch(int numAc, XPilotDataRef& DR, int& outSizeLT,
    std::unique_ptr<T[]>& vBulk)
{
    // later return value: Did we add any new objects?
    bool ret = false;

    // Size negotiation first (we need to do that before _every_ call
    // because in theory there could be another plugin using a different
    // version of LTAPI doing calls before or after us).
    // Array element size will always be set by LTAPI.
    // The return value (size filled by LT) will become important once
    // size _can_ differ at all and we need to cater for LTAPI being
    // bigger than what LT fills. Not yet possible as this is the
    // initial version on both sides. So we don't yet use the result.
    outSizeLT = DR.getData(NULL, 0, sizeof(T));

    // outer loop: get bulk data (iBulkAc number of a/c per request) from LT
    for (int ac = 0;
        ac < numAc;
        ac += iBulkAc)
    {
        // get a bulk of data from LiveTraffic
        // (std::min(...iBulkAc) makes sure we don't exceed our array)
        const int acRcvd = std::min(DR.getData(vBulk.get(),
            ac * sizeof(T),
            iBulkAc * sizeof(T)) / int(sizeof(T)),
            iBulkAc);

        // inner loop: copy the received data into the aircraft objects
        for (int i = 0; i < acRcvd; i++)
        {
            const T& bulk = vBulk[i];

            // try to find the matching aircraft object in out map
            const std::string key = LTAPI::hexStr(bulk.keyNum);
            MapLTAPIAircraft::iterator iter = mapAc.find(key);
            if (iter == mapAc.end())            // didn't find, need new one
            {
                // create a new aircraft object
                assert(pfCreateAcObject);
                iter = mapAc.emplace(key, pfCreateAcObject()).first;
                // tell caller we added new objects
                ret = true;
            }

            // copy the bulk data
            assert(iter != mapAc.end());
            iter->second->updateAircraft(bulk, outSizeLT);
        } // inner loop processing received bulk data
    } // outer loop fetching bulk data from LT

    return ret;
}


XPilotAPIConnect::~XPilotAPIConnect()
{}

bool XPilotAPIConnect::isXPilotAvail()
{
    return XPLMFindPluginBySignature(XPILOT_PLUGIN_SIGNATURE) != XPLM_NO_PLUGIN_ID;
}

XPilotDataRef::XPilotDataRef(std::string _sDataRef) :
    sDataRef(_sDataRef)
{}

// Found the dataRef and it contains formats we can work with?
/// @note Not const! Will call FindDataRef() to try becoming valid.
bool XPilotDataRef::isValid()
{
    if (needsInit()) FindDataRef();
    return bValid;
}

// binds to the dataRef and sets bValid
bool XPilotDataRef::FindDataRef()
{
    dataRef = XPLMFindDataRef(sDataRef.c_str());
    // check available data types; we only work with a subset
    dataTypes = dataRef ? (XPLMGetDataRefTypes(dataRef) & usefulTypes) : xplmType_Unknown;
    return bValid = dataTypes != xplmType_Unknown;
}

int XPilotDataRef::getInt()
{
    if (needsInit()) FindDataRef();
    return XPLMGetDatai(dataRef);
}

float XPilotDataRef::getFloat()
{
    if (needsInit()) FindDataRef();
    return XPLMGetDataf(dataRef);
}

int XPilotDataRef::getData(void* pOut, int inOffset, int inMaxBytes)
{
    if (needsInit()) FindDataRef();
    return XPLMGetDatab(dataRef, pOut, inOffset, inMaxBytes);
}

void XPilotDataRef::set(int i)
{
    if (needsInit()) FindDataRef();
    XPLMSetDatai(dataRef, i);
}

void XPilotDataRef::set(float f)
{
    if (needsInit()) FindDataRef();
    XPLMSetDataf(dataRef, f);
}