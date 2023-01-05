#include <crypto/sha256.h>
#include <storage/manager.h>
#include <storage/serialize.h>
#include <storage/util.h>
#include <uint256.h>

uint32_t container_size(uint32_t pos)
{
    return (4 + (24 * (pos + 1)));
}

void pack_raw_into_container(char* cont, char* mem, uint32_t pos)
{
    memcpy(cont + (4 + (24 * pos)), mem, 24);
}

void pack_obj_into_container(char* cont, struct StorageNode in, uint32_t pos)
{
    char rawbytes[24];
    memcpy(rawbytes, &in, sizeof(struct StorageNode));
    memcpy(cont + (4 + (24 * pos)), rawbytes, 24);
}

struct StorageNode unpack_obj_from_container(char* cont, uint32_t pos)
{
    char rawbytes[24];
    memcpy(rawbytes, cont + (4 + (24 * pos)), 24);
    return deserialize_nodeinfo(rawbytes);
}

char* create_new_container(char* cont, uint32_t pos)
{
    uint32_t max_sz = container_size(pos);
    cont = (char*)malloc(max_sz);
    memcpy(cont, &pos, 4);
    return cont;
}

uint32_t get_elements_in_container(char* cont)
{
    return *(uint32_t*)&cont[0];
}

uint32_t get_bytes_in_container(char* cont)
{
    uint32_t elem = get_elements_in_container(cont);
    return container_size(elem);
}

void free_container(char* cont)
{
    free(cont);
}

void serialize_nodeinfo(struct StorageNode in, char* rawbytes)
{
    memcpy(rawbytes, &in, sizeof(struct StorageNode));
}

struct StorageNode deserialize_nodeinfo(char* rawbytes)
{
    struct StorageNode des;
    memcpy(&des, rawbytes, sizeof(struct StorageNode));
    return des;
}
