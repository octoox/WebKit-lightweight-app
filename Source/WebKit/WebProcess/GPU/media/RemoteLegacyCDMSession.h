/*
 * Copyright (C) 2020 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if ENABLE(GPU_PROCESS) && ENABLE(LEGACY_ENCRYPTED_MEDIA)

#include "MessageReceiver.h"
#include "RemoteLegacyCDMSessionIdentifier.h"
#include <WebCore/LegacyCDMSession.h>
#include <wtf/RefCounted.h>
#include <wtf/WeakPtr.h>

namespace WebCore {
class SharedBuffer;
}

namespace WebKit {

class RemoteLegacyCDMFactory;

class RemoteLegacyCDMSession final
    : public WebCore::LegacyCDMSession
    , public IPC::MessageReceiver
    , public RefCounted<RemoteLegacyCDMSession> {
public:
    static RefPtr<RemoteLegacyCDMSession> create(RemoteLegacyCDMFactory&, RemoteLegacyCDMSessionIdentifier&&, WebCore::LegacyCDMSessionClient&);
    ~RemoteLegacyCDMSession();

    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

    // MessageReceiver
    void didReceiveMessage(IPC::Connection&, IPC::Decoder&) final;

    const RemoteLegacyCDMSessionIdentifier& identifier() const { return m_identifier; }

private:
    RemoteLegacyCDMSession(RemoteLegacyCDMFactory&, RemoteLegacyCDMSessionIdentifier&&, WebCore::LegacyCDMSessionClient&);

    // LegacyCDMSession
    void invalidate() final;
    WebCore::LegacyCDMSessionType type() final { return WebCore::CDMSessionTypeRemote; }
    const String& sessionId() const final { return m_sessionId; }
    RefPtr<Uint8Array> generateKeyRequest(const String& mimeType, Uint8Array* initData, String& destinationURL, unsigned short& errorCode, uint32_t& systemCode) final;
    void releaseKeys() final;
    bool update(Uint8Array*, RefPtr<Uint8Array>& nextMessage, unsigned short& errorCode, uint32_t& systemCode) final;
    RefPtr<ArrayBuffer> cachedKeyForKeyID(const String&) const final;
    bool isRemoteLegacyCDMSession() const final { return true; }

    // Messages
    void sendMessage(RefPtr<WebCore::SharedBuffer>&& message, const String& destinationURL);
    void sendError(WebCore::LegacyCDMSessionClient::MediaKeyErrorCode, uint32_t systemCode);

    WeakPtr<RemoteLegacyCDMFactory> m_factory;
    RemoteLegacyCDMSessionIdentifier m_identifier;
    WeakPtr<WebCore::LegacyCDMSessionClient> m_client;
    String m_sessionId;
    mutable HashMap<String, RefPtr<ArrayBuffer>> m_cachedKeyCache;
};

}

SPECIALIZE_TYPE_TRAITS_BEGIN(WebKit::RemoteLegacyCDMSession)
    static bool isType(const WebCore::LegacyCDMSession& session) { return session.isRemoteLegacyCDMSession(); }
SPECIALIZE_TYPE_TRAITS_END()

#endif
