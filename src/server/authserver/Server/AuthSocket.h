/*
 * Copyright (C) 2010-2012 Oregon <http://www.oregoncore.com/>
 * Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2012 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _AUTHSOCKET_H
#define _AUTHSOCKET_H

#include "Common.h"
#include "BigNumber.h"
#include "SHA1.h"
#include "ByteBuffer.h"

#include "BufferedSocket.h"

// Handle login commands
class AuthSocket: public BufferedSocket
{
    public:
        const static int s_BYTE_SIZE = 32;

        AuthSocket();
        ~AuthSocket();

        void OnAccept();
        void OnRead();
        void SendProof(SHA1Hash sha);
        void LoadRealmlist(ByteBuffer &pkt, uint32 acctid);

        bool _HandleLogonChallenge();
        bool _HandleLogonProof();
        bool _HandleReconnectChallenge();
        bool _HandleReconnectProof();
        bool _HandleRealmList();
        // data transfer handle for patch

        bool _HandleXferResume();
        bool _HandleXferCancel();
        bool _HandleXferAccept();

        void _SetVSFields(const std::string& rI);

    private:

        BigNumber N, s, g, v;
        BigNumber b, B;
        BigNumber K;
        BigNumber _reconnectProof;

        bool _authed;

        std::string _login;
        std::string _safelogin;

        // Since GetLocaleByName() is _NOT_ bijective, we have to store the locale as a string. Otherwise we can't differ
        // between enUS and enGB, which is important for the patch system
        std::string _localizationName;
        uint16 _build;
        AccountTypes _accountSecurityLevel;

        ACE_HANDLE patch_;

        void InitPatch();
};
#endif

