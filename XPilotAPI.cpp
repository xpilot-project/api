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

#define ASSIGN_DR_NAME(var,drName,dataRef,type)     \
static XPilotDataRef DR##drName(dataRef);           \
var = DR##drName.get##type();

#define RETURN_DR(dataRef,type)   \
static XPilotDataRef DR(dataRef); \
return DR.get##type();

#define ZERO_TERM(str) str[sizeof(str)-1] = 0

namespace XPilotAPI {
    std::string hexStr(uint64_t n, unsigned minChars = 6)
    {
        char buf[11] = { 0,0,0,0,0,0,0,0,0,0,0 };
        char* last = buf + sizeof(buf) - 2;
        while (last != buf)
        {
            auto c = n % 16;
            *--last = "0123456789ABCDEF"[c];
            n /= 16;
            if (n == 0)
            {
                if (minChars > sizeof(buf) - 1)
                    minChars = sizeof(buf) - 1;
                while (buf + sizeof(buf) - 1 - minChars < last)
                    *--last = '0';
                return last;
            }
        }
        return "-OVFL-";
    }
}

XPilotAPIAircraft::XPilotAPIAircraft()
{}

XPilotAPIAircraft::~XPilotAPIAircraft()
{}

std::string
XPilotAPIAircraft::getDescription() const
{
    std::string ret;

    // 1. identifier
    ret = info.callSign[0] ? info.callSign : key;

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

// Copies the provided `bulk` data and sets `bUpdated` to `true`
// if the provided data matches this aircraft.
// @note This function can _set_ this object's `key` for the first and only time.
bool
XPilotAPIAircraft::updateAircraft(const XPilotAPIBulkData& __bulk, size_t __inSize)
{
    if (key.empty()) {
        keyNum = (unsigned)__bulk.keyNum;
        key = XPilotAPI::hexStr(__bulk.keyNum);
    }
    else {
        if (__bulk.keyNum != keyNum)
            return false;
    }

    bulk = __bulk;
    bUpdated = true;
    return true;
}

// Copies the provided `info` data and sets `bUpdated` to `true`
// if the provided data matches this aircraft.
bool
XPilotAPIAircraft::updateAircraft(const XPilotAPIBulkInfoTexts& __info, size_t __inSize)
{
    if (__info.keyNum != keyNum)
        return false;

    info = __info;

    ZERO_TERM(info.modelIcao);
    ZERO_TERM(info.callSign);
    ZERO_TERM(info.squawk);
    ZERO_TERM(info.origin);
    ZERO_TERM(info.destination);
    ZERO_TERM(info.cslModel);

    bUpdated = true;
    return true;
}

XPilotAPIConnect::XPilotAPIConnect(fCreateAcObject* _pfCreateAcObject, int numBulkAc) :
    iBulkAc(numBulkAc < 1 ? 1 : numBulkAc > 100 ? 100 : numBulkAc),
    vBulkNum(new XPilotAPIAircraft::XPilotAPIBulkData[iBulkAc]),
    vInfoTexts(new XPilotAPIAircraft::XPilotAPIBulkInfoTexts[iBulkAc]),
    pfCreateAcObject(_pfCreateAcObject)
{}

bool 
XPilotAPIConnect::doesXPilotControlAI()
{
    RETURN_DR("xpilot/ai_controlled", Bool);
}

int 
XPilotAPIConnect::getXPilotNumAc()
{
    RETURN_DR("xpilot/num_aircraft", Int);
}

const 
MapXPilotAPIAircraft& XPilotAPIConnect::UpdateAcList(ListXPilotAPIAircraft* plistRemovedAc)
{
    static XPilotDataRef DRquick("xpilot/bulk/quick");
    static XPilotDataRef DRexpsv("xpilot/bulk/expensive");

    const int numAc = isXPilotAvail() && DRquick.isValid() && DRexpsv.isValid() ? getXPilotNumAc() : 0;
    if (numAc <= 0) {
        if (plistRemovedAc) {
            for (MapXPilotAPIAircraft::value_type& p : mapAc) {
                plistRemovedAc->emplace_back(std::move(p.second));
            }
        }
        mapAc.clear();
        return mapAc;
    }

    for (MapXPilotAPIAircraft::value_type& p : mapAc) {
        p.second->resetUpdated();
    }

    int sizeXPStruct = 0;
    if (DoBulkFetch<XPilotAPIAircraft::XPilotAPIBulkData>(numAc, DRquick, sizeXPStruct, vBulkNum)
        || std::chrono::steady_clock::now() - lastExpsvFetch > sPeriodExpsv)
    {
        sizeXPStruct = 0;
        DoBulkFetch<XPilotAPIAircraft::XPilotAPIBulkInfoTexts>(numAc, DRexpsv, sizeXPStruct, vInfoTexts);
        lastExpsvFetch = std::chrono::steady_clock::now();
    }

    for (MapXPilotAPIAircraft::iterator iter = mapAc.begin(); iter != mapAc.end();)
    {
        if (!iter->second->isUpdated()) {
            if (plistRemovedAc) {
                plistRemovedAc->emplace_back(std::move(iter->second));
            }
            iter = mapAc.erase(iter);
        }
        else {
            iter++;
        }
    }
    return mapAc;
}

// Finds an aircraft for a given multiplayer slot
SPtrXPilotAPIAircraft 
XPilotAPIConnect::getAcByMultIdx(int multiIdx) const
{
    if (multiIdx < 1) {
        return SPtrXPilotAPIAircraft();
    }

    MapXPilotAPIAircraft::const_iterator iter =
        std::find_if(mapAc.cbegin(), mapAc.cend(),
            [multiIdx](const MapXPilotAPIAircraft::value_type& pair)
    { return pair.second->getMultiIdx() == multiIdx; });

    return iter == mapAc.cend() ? SPtrXPilotAPIAircraft() : iter->second;
}

// fetch bulk data and create/update aircraft objects
template <class T>
bool XPilotAPIConnect::DoBulkFetch(int numAc, XPilotDataRef& DR, int& outSizeXP,
    std::unique_ptr<T[]>& vBulk)
{
    bool ret = false;

    outSizeXP = DR.getData(NULL, 0, sizeof(T));

    for (int ac = 0;
        ac < numAc;
        ac += iBulkAc)
    {
        const int acRcvd = std::min(DR.getData(vBulk.get(),
            ac * sizeof(T),
            iBulkAc * sizeof(T)) / int(sizeof(T)),
            iBulkAc);

        for (int i = 0; i < acRcvd; i++)
        {
            const T& bulk = vBulk[i];

            const std::string key = XPilotAPI::hexStr(bulk.keyNum);
            MapXPilotAPIAircraft::iterator iter = mapAc.find(key);
            if (iter == mapAc.end())
            {
                assert(pfCreateAcObject);
                iter = mapAc.emplace(key, pfCreateAcObject()).first;
                ret = true;
            }

            assert(iter != mapAc.end());
            iter->second->updateAircraft(bulk, outSizeXP);
        }
    }
    return ret;
}

XPilotAPIConnect::~XPilotAPIConnect()
{}

bool 
XPilotAPIConnect::isXPilotAvail()
{
    return XPLMFindPluginBySignature(XPILOT_PLUGIN_SIGNATURE) != XPLM_NO_PLUGIN_ID;
}

XPilotDataRef::XPilotDataRef(std::string _sDataRef) :
    sDataRef(_sDataRef)
{}

bool 
XPilotDataRef::isValid()
{
    if (needsInit()) FindDataRef();
    return bValid;
}

bool 
XPilotDataRef::FindDataRef()
{
    dataRef = XPLMFindDataRef(sDataRef.c_str());
    dataTypes = dataRef ? (XPLMGetDataRefTypes(dataRef) & usefulTypes) : xplmType_Unknown;
    return bValid = dataTypes != xplmType_Unknown;
}

int 
XPilotDataRef::getInt()
{
    if (needsInit()) FindDataRef();
    return XPLMGetDatai(dataRef);
}

float 
XPilotDataRef::getFloat()
{
    if (needsInit()) FindDataRef();
    return XPLMGetDataf(dataRef);
}

int 
XPilotDataRef::getData(void* pOut, int inOffset, int inMaxBytes)
{
    if (needsInit()) FindDataRef();
    return XPLMGetDatab(dataRef, pOut, inOffset, inMaxBytes);
}

void 
XPilotDataRef::set(int i)
{
    if (needsInit()) FindDataRef();
    XPLMSetDatai(dataRef, i);
}

void 
XPilotDataRef::set(float f)
{
    if (needsInit()) FindDataRef();
    XPLMSetDataf(dataRef, f);
}