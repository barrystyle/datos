#ifndef STORAGE_SERIALIZE_H
#define STORAGE_SERIALIZE_H

#include <crypto/sha256.h>
#include <storage/proof.h>
#include <uint256.h>

struct StorageNode {
    uint32_t id;
    uint32_t ip;
    uint8_t mode;
    uint8_t stat;
    uint8_t reg;
    uint8_t load;
    uint32_t chunks;
    uint32_t errcnt;
    uint32_t space;

    uint256 GetHash()
    {
        uint256 h;
        CSHA256 sha256;
        sha256.Write((const unsigned char*)this, 24);
        sha256.Finalize(h.begin());
        return h;
    }

    SERIALIZE_METHODS(StorageNode, obj)
    {
        READWRITE(obj.id);
        READWRITE(obj.ip);
        READWRITE(obj.mode);
        READWRITE(obj.stat);
        READWRITE(obj.reg);
        READWRITE(obj.load);
        READWRITE(obj.chunks);
        READWRITE(obj.errcnt);
        READWRITE(obj.space);
    }
};

uint32_t container_size(uint32_t pos);
void pack_raw_into_container(char* cont, char* mem, uint32_t pos);
void pack_obj_into_container(char* cont, struct StorageNode in, uint32_t pos);
struct StorageNode unpack_obj_from_container(char* cont, uint32_t pos);
char* create_new_container(char* cont, uint32_t pos);
uint32_t get_elements_in_container(char* cont);
uint32_t get_bytes_in_container(char* cont);
void free_container(char* cont);
void serialize_nodeinfo(struct StorageNode in, char* rawbytes);
struct StorageNode deserialize_nodeinfo(char* rawbytes);

#endif // STORAGE_SERIALIZE_H
