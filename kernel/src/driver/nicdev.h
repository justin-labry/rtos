#ifndef __NICDEV_H__
#define __NICDEV_H__

#include <vnic.h>

#define MAX_NIC_DEVICE_COUNT	128
#define MAX_NIC_NAME_LEN	16

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

typedef struct _NICDevice{
	char		name[MAX_NIC_NAME_LEN];
	uint64_t	mac;
	int		mtu;
	uint16_t	vlan_proto; ///< VLAN Protocol
	uint16_t	vlan_tci;   ///< VLAN TCI

	void*		driver;
	void*		priv;

	VNIC*		vnics[MAX_VNIC_COUNT];
	int		vnics_count;

	uint16_t	round; //FIXME: current nicdev only support round robin schedule

	struct _NICDevice* next;
	struct _NICDevice* prev;
} NICDevice;

typedef struct {
	char		name[MAX_NIC_NAME_LEN];
	uint64_t	mac;
} NICInfo;

typedef struct {
	int		mtu;
} NICStatus;

typedef struct {
	int		(*init)(void* device, void* data);
	void		(*destroy)(NICDevice* nicdev);
	bool		(*tx_poll)(NICDevice* nicdev);
	int 		(*poll)(NICDevice* nicdev);
	bool 		(*xmit)(NICDevice* nicdev, Packet* packet);

	void		(*get_status)(NICDevice* nicdev, NICStatus* status);
	bool		(*set_status)(NICDevice* nicdev, NICStatus* status);
	void		(*get_info)(NICDevice* nicdev, NICInfo* info);

	bool 		(*add_vid)(NICDevice* nicdev, uint16_t vid);
	bool 		(*remove_vid)(NICDevice* nicdev, uint16_t vid);
} NICDriver;

typedef enum _NICDEV_PROCESS_TYPE {
		NICDEV_RX_PROCESS = 1,
		NICDEV_TX_PROCESS,
		NICDEV_SRX_PROCESS,
		NICDEV_STX_PROCESS,
} NICDEV_PROCESS_TYPE;

int nicdev_process_register(bool (*process)(void*, size_t, void*), void* context, NICDEV_PROCESS_TYPE type);
int nicdev_process_unregister(NICDEV_PROCESS_TYPE type);

NICDevice* nicdev_create();
void nicdev_destroy(NICDevice* dev);
int nicdev_register(NICDevice* dev);
NICDevice* nicdev_unregister(const char* name);

/**
 * Get default NICDevice
 * 
 * @return NIC Device
 */
NICDevice* nicdev_get_default();

/**
 * @param name NIC Device name
 * 
 * @return NIC Device
 */
NICDevice* nicdev_get(const char* name);
int nicdev_poll();
void nicdev_free(Packet* packet);

/**
 * Register VNIC to NICDevice
 *
 * @param nicdev NIC Device
 * @param vnic Virtual NIC
 *
 * @param vnic id
 */
int nicdev_register_vnic(NICDevice* nicdev, VNIC* vnic);
VNIC* nicdev_unregister_vnic(NICDevice* nicdev, uint32_t id);
VNIC* nicdev_get_vnic(NICDevice* nicdev, uint32_t id);
VNIC* nicdev_get_vnic_mac(NICDevice* nicdev, uint64_t mac);
VNIC* nicdev_get_vnic_name(NICDevice* nicdev, char* name);
VNIC* nicdev_update_vnic(NICDevice* nicdev, VNIC* src_vnic);

enum NICDEV_PROCESS_RESULT {
	NICDEV_PROCESS_COMPLETE,
	NICDEV_PROCESS_PASS,
};

#define	NICDEV_DEBUG_OFF			0
#define	NICDEV_DEBUG_PACKET_INFO		1 << 1
#define	NICDEV_DEBUG_PACKET_DUMP		1 << 2
#define NICDEV_DEBUG_PACKET_ETHER_INFO		1 << 3
#define NICDEV_DEBUG_PACKET_VERBOSE_INFO	1 << 4

/**
 * @param debug option
 */
void nidev_debug_switch_set(uint8_t opt);

/**
 * @return debug option
 */
uint8_t nicdev_debug_switch_get();

/**
 * @param dev NIC device
 * @param data data to be sent
 * @param size data size
 *
 * @return result of process
 */
int nicdev_rx(NICDevice* dev, void* data, size_t size);

/**
 * @param dev NIC Device
 * @param data data to be sent
 * @param size data size
 * @param data_optional optional data to be sent
 * @param size_optional opttional data size
 *
 * @return  result of process
 */
int nicdev_rx0(NICDevice* dev, void* data, size_t size, void* data_optional, size_t size_optional);

/**
 * @param dev NIC device
 * @param process function to process packets in NIC device
 * @param context context to be passed to process function
 *
 * @return number of packets proccessed
 */

int nicdev_tx(NICDevice* dev, bool (*process)(Packet* packet, void* context), void* context);
/**
 * @param dev NIC device
 * @param data data to be sent
 * @param size data size
 *
 * @return result of process
 */
int nicdev_srx(VNIC* vnic, void* data, size_t size);

/**
 * @param dev NIC Device
 * @param data data to be sent
 * @param size data size
 * @param data_optional optional data to be sent
 * @param size_optional opttional data size
 *
 * @return  result of process
 */
int nicdev_srx0(VNIC* vnic, void* data, size_t size, void* data_optional, size_t size_optional);

/**
 * @param dev NIC device
 * @param process function to process packets in NIC device
 * @param context context to be passed to process function
 *
 * @return number of packets proccessed
 */
int nicdev_stx(VNIC* vnic, bool (*process)(Packet* packet, void* context), void* context);
/**
 * @param dev NIC Device
 * @param TCI
 *
 * @return result of NIC Device
 */
NICDevice* nicdev_add_vlan(NICDevice* nicdev, uint16_t id);

/**
 * @param dev NIC Device
 *
 * @return result of vlan remove
 */
bool nicdev_remove_vlan(NICDevice* dev);
#endif /* __NICDEV_H__ */
