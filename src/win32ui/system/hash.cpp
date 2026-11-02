// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/system/hash.h"

#include "win32ui/handle_owner.h"
#include "win32ui/system/error_text.h"

#include <bcrypt.h>

#include <algorithm>
#include <vector>

namespace win32ui
{

namespace
{

struct AlgorithmCloser
{
    void operator()(BCRYPT_ALG_HANDLE algorithm) const noexcept
    {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
};

struct HashCloser
{
    void operator()(BCRYPT_HASH_HANDLE hash) const noexcept
    {
        BCryptDestroyHash(hash);
    }
};

}

DWORD Sha256(std::string_view bytes, Sha256Digest* digest)
{
    UniqueNullable<BCRYPT_ALG_HANDLE, AlgorithmCloser> algorithm;
    // Vista needs a hash object from the caller, it outlives the hash handle
    std::vector<UCHAR> object;
    UniqueNullable<BCRYPT_HASH_HANDLE, HashCloser> hash;
    NTSTATUS status = BCryptOpenAlgorithmProvider(algorithm.put(), BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    DWORD object_size = 0;
    ULONG written = 0;
    if (BCRYPT_SUCCESS(status))
    {
        status = BCryptGetProperty(algorithm.get(), BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &written, 0);
    }
    if (BCRYPT_SUCCESS(status))
    {
        object.resize(object_size);
        status = BCryptCreateHash(algorithm.get(), hash.put(), object.data(), object_size, nullptr, 0, 0);
    }
    // ULONG pieces, the data may pass 4 GB
    for (size_t done = 0; BCRYPT_SUCCESS(status) && done < bytes.size();)
    {
        const ULONG count = static_cast<ULONG>((std::min<size_t>)(bytes.size() - done, 0x40000000));
        status = BCryptHashData(hash.get(), reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data() + done)), count, 0);
        done += count;
    }
    if (BCRYPT_SUCCESS(status))
    {
        status = BCryptFinishHash(hash.get(), digest->data(), static_cast<ULONG>(digest->size()), 0);
    }
    return Win32FromNtStatus(status);
}

}
