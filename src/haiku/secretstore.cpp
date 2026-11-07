///////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/secretstore.cpp
// Purpose:     wxSecretStore implementation for Haiku using libsodium
// Author:      (your name)
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

//#if wxUSE_SECRETSTORE

#include "wx/secretstore.h"
#include "wx/private/secretstore.h"
#include "wx/filename.h"
#include "wx/stdpaths.h"
#include "wx/filefn.h"
#include "wx/log.h"

#include <sodium.h>

#include <vector>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits>

namespace
{

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

constexpr size_t KEY_SIZE   = crypto_secretbox_KEYBYTES;
constexpr size_t NONCE_SIZE = crypto_secretbox_NONCEBYTES;

wxFileName GetBaseDir()
{
    wxFileName fn = wxFileName::DirName(wxStandardPaths::Get().GetUserConfigDir());
    fn.AppendDir("wxwidgets");
    fn.AppendDir("secrets");
    fn.Mkdir(0700, wxPATH_MKDIR_FULL);
    return fn;
}

wxFileName GetKeyFile()
{
    wxFileName fn = wxFileName::DirName(wxStandardPaths::Get().GetUserConfigDir());
    fn.AppendDir("wxwidgets");
    fn.Mkdir(0700, wxPATH_MKDIR_FULL);
    fn.SetFullName("secret.key");
    return fn;
}
wxFileName GetSecretFile(const wxString& service)
{
    const wxCharBuffer utf8 = service.utf8_str();
    unsigned char digest[32];
    crypto_generichash(digest, sizeof digest,
        reinterpret_cast<const unsigned char*>(utf8.data()), utf8.length(), nullptr, 0);
    char hex[65];
    sodium_bin2hex(hex, sizeof hex, digest, sizeof digest);
    return wxFileName(GetBaseDir().GetFullPath(), wxString("v2-") + hex);
}

// Old filenames were ambiguous after slash replacement. Only use unambiguous
// legacy names; never interpret a new-format filename as a legacy service.
wxFileName GetLegacySecretFile(const wxString& service)
{
    if (service.empty() || service.StartsWith("v2-") || service == "." || service == "..")
        return wxFileName();
    for (wxUniChar c : service)
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
            !(c >= '0' && c <= '9') && c != '-') return wxFileName();
    return wxFileName(GetBaseDir().GetFullPath(), service);
}

bool ReadAll(const wxFileName& fn, std::vector<unsigned char>& out)
{
    std::ifstream file(fn.GetFullPath().ToStdString(), std::ios::binary);
    if (!file) return false;
    file.seekg(0, std::ios::end);
    const std::streamoff length = file.tellg();
    if (length < 0 || static_cast<uint64_t>(length) > 64*1024*1024) return false;
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(length));
    if (length && !file.read(reinterpret_cast<char*>(out.data()), length))
    { out.clear(); return false; }
    return true;
}

bool WriteDescriptor(int fd, const unsigned char* data, size_t length)
{
    while (length)
    {
        ssize_t written = write(fd, data, length);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        data += written; length -= written;
    }
    return fsync(fd) == 0;
}

bool WriteAll(const wxFileName& fn, const unsigned char* data, size_t length)
{
    std::string path = fn.GetFullPath().ToStdString();
    std::string temporary = path + ".tmp-XXXXXX";
    std::vector<char> name(temporary.begin(), temporary.end()); name.push_back(0);
    int fd = mkstemp(name.data());
    if (fd < 0) return false;
    bool ok = WriteDescriptor(fd, data, length);
    if (close(fd) != 0) ok = false;
    if (ok) ok = rename(name.data(), path.c_str()) == 0;
    if (!ok) unlink(name.data());
    return ok;
}

void AppendU32(std::vector<unsigned char>& v, uint32_t x)
{
    v.push_back((x >> 24) & 0xff);
    v.push_back((x >> 16) & 0xff);
    v.push_back((x >> 8) & 0xff);
    v.push_back(x & 0xff);
}

uint32_t ReadU32(const unsigned char*& p)
{
    uint32_t x =
        (uint32_t(p[0]) << 24) |
        (uint32_t(p[1]) << 16) |
        (uint32_t(p[2]) << 8)  |
        (uint32_t(p[3]));
    p += 4;
    return x;
}

// ---------------------------------------------------------------------------
// wxSecretValue (Haiku)
// ---------------------------------------------------------------------------

class wxSecretValueHaikuImpl : public wxSecretValueImpl
{
public:
    wxSecretValueHaikuImpl(size_t size, const void* data)
        : m_buf(size)
    {
        memcpy(m_buf.data(), data, size);
    }

    ~wxSecretValueHaikuImpl() override
    {
        sodium_memzero(m_buf.data(), m_buf.size());
    }

    size_t GetSize() const override { return m_buf.size(); }
    const void* GetData() const override { return m_buf.data(); }

private:
    std::vector<unsigned char> m_buf;
};

// ---------------------------------------------------------------------------
// wxSecretStore (Haiku)
// ---------------------------------------------------------------------------

class wxSecretStoreHaikuImpl : public wxSecretStoreImpl
{
public:
    wxSecretStoreHaikuImpl()
    {
        if (sodium_init() < 0)
            return;

        LoadOrCreateKey();
    }

    ~wxSecretStoreHaikuImpl() override { sodium_memzero(m_key, sizeof m_key); }

    bool IsOk(wxString* errmsg) const override
    {
        if (!m_keyLoaded)
        {
            if (errmsg)
                *errmsg = "libsodium key not available";
            return false;
        }
        return true;
    }

    bool Save(const wxString& service,
              const wxString& user,
              const wxSecretValueImpl& secret,
              wxString& errmsg) override
    {
        if (!m_keyLoaded)
        {
            errmsg = "Secret store not initialized";
            return false;
        }

        if (secret.GetSize() > 64*1024*1024 - 2048 || user.utf8_str().length() > 1024)
        { errmsg = "Secret record too large"; return false; }
        std::vector<unsigned char> plain;

        const wxCharBuffer userUtf8 = user.utf8_str();
        AppendU32(plain, userUtf8.length());
        plain.insert(plain.end(),
                     userUtf8.data(),
                     userUtf8.data() + userUtf8.length());

        AppendU32(plain, secret.GetSize());
        const unsigned char* s =
            static_cast<const unsigned char*>(secret.GetData());
        plain.insert(plain.end(), s, s + secret.GetSize());

        unsigned char nonce[NONCE_SIZE];
        randombytes_buf(nonce, sizeof nonce);

        std::vector<unsigned char> cipher(
            plain.size() + crypto_secretbox_MACBYTES);

        if (crypto_secretbox_easy(cipher.data(),
                                  plain.data(),
                                  plain.size(),
                                  nonce,
                                  m_key) != 0)
        {
            errmsg = "Encryption failed";
            return false;
        }

        wxFileName fn = GetSecretFile(service);
        std::vector<unsigned char> record(nonce, nonce + sizeof nonce);
        record.insert(record.end(), cipher.begin(), cipher.end());
        if (!WriteAll(fn, record.data(), record.size()))
        {
            errmsg = "Failed to write secret file";
            return false;
        }

        return true;
    }

    bool Load(const wxString& service,
              wxString* user,
              wxSecretValueImpl** secret,
              wxString& errmsg) const override
    {
        if (!m_keyLoaded)
            return false;

        wxFileName fn = GetSecretFile(service);
        std::vector<unsigned char> data;
        if (!fn.FileExists())
        {
            const wxFileName legacy = GetLegacySecretFile(service);
            if (legacy.IsOk()) fn = legacy;
        }
        if (!ReadAll(fn, data)) return false;

        if (data.size() <= NONCE_SIZE + crypto_secretbox_MACBYTES)
        {
            errmsg = "Corrupted secret file";
            return false;
        }

        const unsigned char* nonce = data.data();
        const unsigned char* cipher = data.data() + NONCE_SIZE;
        size_t cipherLen = data.size() - NONCE_SIZE;

        std::vector<unsigned char> plain(
            cipherLen - crypto_secretbox_MACBYTES);

        if (crypto_secretbox_open_easy(plain.data(),
                                       cipher,
                                       cipherLen,
                                       nonce,
                                       m_key) != 0)
        {
            errmsg = "Decryption failed";
            return false;
        }

        const unsigned char* p = plain.data();

        if (plain.size() < 8)
        {
            errmsg = "Corrupted secret payload";
            return false;
        }
        uint32_t userLen = ReadU32(p);
        if (userLen > plain.size() - 8)
        {
            errmsg = "Corrupted secret user length";
            return false;
        }
        if (user && userLen)
        {
            *user = wxString::FromUTF8(
                reinterpret_cast<const char*>(p),
                userLen);
        }
        p += userLen;

        uint32_t secLen = ReadU32(p);
        if (secLen != plain.size() - 8 - userLen)
        {
            errmsg = "Corrupted secret length";
            return false;
        }
        *secret = new wxSecretValueHaikuImpl(secLen, p);

        return true;
    }

    bool Delete(const wxString& service,
                wxString&) override
    {
        wxFileName fn = GetSecretFile(service);
        bool ok = !fn.FileExists() || wxRemoveFile(fn.GetFullPath());
        const wxFileName legacy = GetLegacySecretFile(service);
        if (legacy.IsOk() && legacy.FileExists())
            ok = wxRemoveFile(legacy.GetFullPath()) && ok;
        return ok;
    }

private:
    void LoadOrCreateKey()
    {
        wxFileName fn = GetKeyFile();
        if (fn.FileExists())
        {
            std::vector<unsigned char> buf;
            if (ReadAll(fn, buf) && buf.size() == KEY_SIZE)
            {
                memcpy(m_key, buf.data(), KEY_SIZE);
                m_keyLoaded = true;
                return;
            }
            // Never overwrite an existing unreadable or malformed key.
            return;
        }

        randombytes_buf(m_key, KEY_SIZE);
        const std::string path = fn.GetFullPath().ToStdString();
        int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd >= 0)
        {
            bool ok = WriteDescriptor(fd, m_key, KEY_SIZE);
            if (close(fd) != 0) ok = false;
            m_keyLoaded = ok;
        }
        else if (errno == EEXIST)
        {
            std::vector<unsigned char> existing;
            if (ReadAll(fn, existing) && existing.size() == KEY_SIZE)
            {
                memcpy(m_key, existing.data(), KEY_SIZE);
                sodium_memzero(existing.data(), existing.size());
                m_keyLoaded = true;
            }
        }
    }

    unsigned char m_key[KEY_SIZE]{};
    bool m_keyLoaded{false};
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// wxWidgets hooks
// ---------------------------------------------------------------------------

wxSecretValueImpl*
wxSecretValue::NewImpl(size_t size,
                       const void* data,
                       const char*)
{
    return new wxSecretValueHaikuImpl(size, data);
}

wxSecretStore wxSecretStore::GetDefault()
{
    return wxSecretStore(new wxSecretStoreHaikuImpl);
}

//#endif // wxUSE_SECRETSTORE
