/*
    This file is part of Helio Workstation.

    Helio is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Helio is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Helio. If not, see <http://www.gnu.org/licenses/>.
*/

#include "Common.h"
#include "ArpeggiatorsManager.h"
#include "Arpeggiator.h"
#include "SerializationKeys.h"
#include "BinaryData.h"

#include "App.h"
#include "Config.h"
#include "DataEncoder.h"


void ArpeggiatorsManager::initialise(const String &commandLine)
{
    this->reset();
    this->reloadArps();
    const int requestArpsDelayMs = 2000;
    
    this->startTimer(requestArpsDelayMs);
    //Logger::writeToLog(DataEncoder::obfuscate("http://helioworkstation.com/vcs/arps.php"));
}

void ArpeggiatorsManager::shutdown()
{
    this->reset();
}

Array<Arpeggiator> ArpeggiatorsManager::getArps() const
{
    return this->arps;
}

bool ArpeggiatorsManager::replaceArpWithId(const String &id, const Arpeggiator &arp)
{
    for (int i = 0; i < this->arps.size(); ++i)
    {
        if (this->arps.getUnchecked(i).getId() == id)
        {
            this->arps.setUnchecked(i, arp);
            this->saveConfigArps();
            this->push();
            this->sendChangeMessage();
            return true;
        }
    }
    
    return false;
}

void ArpeggiatorsManager::addArp(const Arpeggiator &arp)
{
    if (! this->replaceArpWithId(arp.getId(), arp))
    {
        this->arps.add(arp);
        this->saveConfigArps();
        this->push();
        this->sendChangeMessage();
    }
}

bool ArpeggiatorsManager::isPullPending() const
{
    if (this->requestArpsThread == nullptr)
    {
        return false;
    }
    
    return this->requestArpsThread->isThreadRunning();
}

bool ArpeggiatorsManager::isPushPending() const
{
    if (this->updateArpsThread == nullptr)
    {
        return false;
    }
    
    return this->updateArpsThread->isThreadRunning();
}

void ArpeggiatorsManager::pull()
{
    if (this->isPullPending() || this->isPushPending())
    {
        return;
    }
    
    this->requestArpsThread = new RequestArpeggiatorsThread();
    this->requestArpsThread->requestArps(this);
}

void ArpeggiatorsManager::push()
{
    if (this->isPullPending() || this->isPushPending())
    {
        return;
    }
    
    ScopedPointer<XmlElement> xml(this->serialize());
    const String xmlString(xml->createDocument("", false, true, "UTF-8", 512));

    this->requestArpsThread = new RequestArpeggiatorsThread();
    this->requestArpsThread->updateArps(xmlString, this);
}


//===----------------------------------------------------------------------===//
// Serializable
//===----------------------------------------------------------------------===//

XmlElement *ArpeggiatorsManager::serialize() const
{
    auto xml = new XmlElement(Serialization::Arps::arpeggiators);
    
    for (int i = 0; i < this->arps.size(); ++i)
    {
        xml->addChildElement(this->arps.getUnchecked(i).serialize());
    }
    
    return xml;
}

void ArpeggiatorsManager::deserialize(const XmlElement &xml)
{
    this->reset();
    
    const XmlElement *root = xml.hasTagName(Serialization::Arps::arpeggiators) ?
        &xml : xml.getChildByName(Serialization::Arps::arpeggiators);
    
    if (root == nullptr) { return; }
    
    forEachXmlChildElementWithTagName(*root, arpXml, Serialization::Arps::arpeggiator)
    {
        Arpeggiator arp;
        arp.deserialize(*arpXml);
        this->arps.add(arp);
    }
    
    this->sendChangeMessage();
}

void ArpeggiatorsManager::reset()
{
    this->arps.clear();
    this->sendChangeMessage();
}


//===----------------------------------------------------------------------===//
// Private
//===----------------------------------------------------------------------===//

void ArpeggiatorsManager::reloadArps()
{
    const String configArps(this->getConfigArps());
    const File debugArps(this->getDebugArpsFile());
    
    if (debugArps.existsAsFile())
    {
        Logger::writeToLog("Found debug arps file, loading..");
        
        //ScopedPointer<XmlElement> xml(DataEncoder::loadObfuscated(debugArps));
        ScopedPointer<XmlElement> xml(XmlDocument::parse(debugArps.loadFileAsString()));

        if (xml != nullptr)
        {
            this->deserialize(*xml);
        }
    }
    else if (configArps.isNotEmpty())
    {
        Logger::writeToLog("Found config arps, loading..");
        ScopedPointer<XmlElement> xml(XmlDocument::parse(configArps));

        if (xml != nullptr)
        {
            this->deserialize(*xml);
        }
    }
    else
    {
        // built-in arps
        const String defaultArps = String(CharPointer_UTF8(BinaryData::DefaultArps_xml));
        ScopedPointer<XmlElement> xml(XmlDocument::parse(defaultArps));
        
        if (xml != nullptr)
        {
            this->deserialize(*xml);
        }
    }
}

void ArpeggiatorsManager::saveConfigArps()
{
    ScopedPointer<XmlElement> xml(this->serialize());
    const String xmlString(xml->createDocument("", false, true, "UTF-8", 512));
    Config::set(Serialization::Arps::arpeggiators, xmlString);
}

String ArpeggiatorsManager::getConfigArps()
{
    return Config::get(Serialization::Arps::arpeggiators);
}


//===----------------------------------------------------------------------===//
// Timer
//===----------------------------------------------------------------------===//

void ArpeggiatorsManager::timerCallback()
{
    this->stopTimer();
    this->pull();
}


//===----------------------------------------------------------------------===//
// RequestTranslationsThread::Listener
//===----------------------------------------------------------------------===//

void ArpeggiatorsManager::arpsRequestOk(RequestArpeggiatorsThread *thread)
{
    Logger::writeToLog("ArpeggiatorsManager::arpsRequestOk");
    
    if (thread == this->requestArpsThread)
    {
        ScopedPointer<XmlElement> xml(XmlDocument::parse(thread->getLastFetchedData()));
        
        if (xml != nullptr)
        {
            this->deserialize(*xml);
            this->saveConfigArps();
        }
    }
}

void ArpeggiatorsManager::arpsRequestFailed(RequestArpeggiatorsThread *thread)
{
    Logger::writeToLog("ArpeggiatorsManager::arpsRequestConnectionFailed");
}


//===----------------------------------------------------------------------===//
// Static
//===----------------------------------------------------------------------===//

File ArpeggiatorsManager::getDebugArpsFile()
{
    static String debugArpsFileName = "ArpsDebug.xml";
    return File::getSpecialLocation(File::currentApplicationFile).getSiblingFile(debugArpsFileName);
}

