//////////////////////////////////////////////////////////////////////
// OpenTibia - an opensource roleplaying game
//////////////////////////////////////////////////////////////////////
//
//////////////////////////////////////////////////////////////////////
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//////////////////////////////////////////////////////////////////////
#include "otpch.h"

#include "protocollogin.h"
#include "outputmessage.h"
#include "connection.h"

#include "rsa.h"
#include "configmanager.h"
#include "tools.h"
#include "ioaccount.h"
#include "ban.h"
#include <iomanip>

extern RSA* g_otservRSA;
extern ConfigManager g_config;
extern IPList serverIPs;
extern Ban g_bans;

#ifdef __DEBUG_NET__
void ProtocolLogin::deleteProtocolTask()
{
	std::cout << "Deleting ProtocolLogin" << std::endl;
	Protocol::deleteProtocolTask();
}
#endif

bool ProtocolLogin::parseFirstPacket(NetworkMessage& msg)
{
	uint16_t clientos = msg.GetU16();
	uint16_t version  = msg.GetU16();
	msg.SkipBytes(12);
	
	if(version < CLIENT_VERSION_MIN || version > CLIENT_VERSION_MAX){
		OutputMessage* output = OutputMessagePool::getInstance()->getOutputMessage(this, false);
		output->AddByte(0x0A);
		output->AddString(STRING_CLIENT_VERSION);
		OutputMessagePool::getInstance()->send(output);
		getConnection()->closeConnection();
		return false;
	}

	if(!RSA_decrypt(g_otservRSA, msg)){
		OutputMessage* output = OutputMessagePool::getInstance()->getOutputMessage(this, false);
		output->AddByte(0x0A);
		output->AddString("RSA decryption failed.");
		OutputMessagePool::getInstance()->send(output);
		getConnection()->closeConnection();
		return false;
	}

	OutputMessage* output = OutputMessagePool::getInstance()->getOutputMessage(this, false);
	uint32_t key[4];
	key[0] = msg.GetU32();
	key[1] = msg.GetU32();
	key[2] = msg.GetU32();
	key[3] = msg.GetU32();
	enableXTEAEncryption();
	setXTEAKey(key);

	/*
	std::cout.flags(std::ios::hex);
	std::cout << std::setw(2) << std::setfill('0') << m_key[0] << " " <<
	std::setw(2) << std::setfill('0') << m_key[1] << " " <<
	std::setw(2) << std::setfill('0') << m_key[2] << " " <<
	std::setw(2) << std::setfill('0') << m_key[3] << std::endl;
	std::cout.flags(std::ios::dec);
	*/

	uint32_t accnumber = msg.GetU32();
	std::string password = msg.GetString();
	uint32_t serverip = serverIPs[0].first;
	uint32_t clientip = getConnection()->getIP();
	for(uint32_t i = 0; i < serverIPs.size(); i++){
		if((serverIPs[i].first & serverIPs[i].second) == (clientip & serverIPs[i].second)){
			serverip = serverIPs[i].first;
			break;
		}
	}

	if(g_bans.isIpDisabled(clientip)){
		output->AddByte(0x0A);
		output->AddString("To many connections attempts from this IP. Try again later.");
		OutputMessagePool::getInstance()->send(output);
		getConnection()->closeConnection();
		return false;
	}
	
	if(g_bans.isIpBanished(clientip)){
		output->AddByte(0x0A);
		output->AddString("Your IP is banished!");
		OutputMessagePool::getInstance()->send(output);
		getConnection()->closeConnection();
		return false;
	}

	Account account = IOAccount::instance()->loadAccount(accnumber);
	if(!(accnumber != 0 && account.accnumber == accnumber &&
			passwordTest(password, account.password))){

		g_bans.addLoginAttempt(clientip, false);
		output->AddByte(0x0A);
		output->AddString("Please enter a valid account number and password.");
		OutputMessagePool::getInstance()->send(output);
		getConnection()->closeConnection();
		return false;
	}

	g_bans.addLoginAttempt(clientip, true);
	output->AddByte(0x14);
	std::stringstream motd;
	motd << g_config.getNumber(ConfigManager::MOTD_NUM) << "\n";
	motd << g_config.getString(ConfigManager::MOTD);
	output->AddString(motd.str());
	output->AddByte(0x64);
	output->AddByte((uint8_t)account.charList.size());

	std::list<std::string>::iterator it;
	for(it = account.charList.begin(); it != account.charList.end(); it++){
		output->AddString((*it));
		output->AddString(g_config.getString(ConfigManager::WORLD_NAME));
		output->AddU32(serverip);
		output->AddU16(g_config.getNumber(ConfigManager::PORT));
	}

	output->AddU16(account.premDays);
	OutputMessagePool::getInstance()->send(output);
	getConnection()->closeConnection();

	return true;
}

void ProtocolLogin::onRecvFirstMessage(NetworkMessage& msg)
{
	parseFirstPacket(msg);
}
