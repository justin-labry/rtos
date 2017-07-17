#ifndef __VNIC_H__
#define __VNIC_H__

#include "nic.h"

#define _IFNAMSIZ		16
#define MAX_VNIC_COUNT		8

/**
 * @file Virtual NIC
 */

/**
 * VNIC attributes to create or update VNIC.
 * @see vm_create() for how this value is used
 */
typedef enum {
	VNIC_NONE = 0,			///< End of attributes
	VNIC_ID,			///< NIC ID
	VNIC_BUDGET,			///< Maximum poll count without swithing to another VNIC

	VNIC__MAND_STRT,
	VNIC_DEV,			///< Device of Network Interface
	VNIC_MAC,			///< MAC address
	VNIC_POOL_SIZE,			///< NI's total memory size
	VNIC_RX_BANDWIDTH,		///< Input bandwidth in bps
	VNIC_TX_BANDWIDTH,		///< Output bandwidth in bps
	VNIC_PADDING_HEAD,		///< Minimum padding head of packet payload buffer
	VNIC_PADDING_TAIL,		///< Minimum padding tail of packet payload buffer
	VNIC_FLAGS,				///< FLAGS
	VNIC__MAND_END,

	VNIC_RX_QUEUE_SIZE,		///< Number of input buffer size, the packet count
	VNIC_TX_QUEUE_SIZE,		///< Number of output buffer size, the packet count
	VNIC_SLOW_RX_QUEUE_SIZE,	///< Number of input slowpath buffer size, the packet count
	VNIC_SLOW_TX_QUEUE_SIZE,	///< Number of input slowpath buffer size, the packet count
	VNIC_MIN_BUFFER_SIZE,		///< Minimum packet payload buffer size which includes padding
	VNIC_MAX_BUFFER_SIZE,		///< Maximum packet payload buffer size which includes padding

	VNIC_RX_ACCEPT_ALL,		///< To accept all packets to receive
	VNIC_RX_ACCEPT,			///< List of accept MAC addresses to receive
	VNIC_TX_ACCEPT_ALL,		///< To accept all packets to send
	VNIC_TX_ACCEPT,			///< List of accept MAC addresses to send
} VNICAttributes;

/**
 * VNIC Error Codes
 */
typedef enum _VNICError {
	VNIC_ERROR_NOERROR = 0,
	VNIC_ERROR_ATTRIBUTE_MISSING,	    ///<Mandatory attributes not specified
	VNIC_ERROR_ATTRIBUTE_CONFLICT,	    ///<Conflict attributes speicifed
	VNIC_ERROR_ATTRIBUTE_INVALID,	    ///<Invalid attributes speicifed
	VNIC_ERROR_INVALID_BASE,	    ///<Base pointer is not pointing correct memory location
	VNIC_ERROR_INVALID_POOLSIZE,	    ///<Pool size is not multiple of 2MB
	VNIC_ERROR_NO_MEMORY,		    ///<Not enough memory
	VNIC_ERROR_RESOURCE_NOT_AVAILABLE,  ///<Resource is not temporary available
	VNIC_ERROR_NO_ID_AVAILABLE,	    ///<There is no more id for vnic
	VNIC_ERROR_OPERATION_FAILED,	    ///<Failed to execute command successfully
	VNIC_ERROR_UNSUPPORTED,		    ///<Unsupported operation were requested
} VNICError;

/**
 * Virtual NIC
 */
typedef struct _VNIC {
	// Management
	NIC*		nic;			    ///< Base address of assosicated NIC (assigned by VM)
	uint32_t	nic_size;		    ///< Pool size of associated NIC (multiples of 2MB)
	char		parent[MAX_NIC_NAME_LEN];   ///< Name of parent NICDevice
	NICPool		pool;			    ///< Pool of this VNIC

	// Information
	uint64_t	magic;			///< Magic
	uint32_t	id;			///< NIC unique ID (unique ID in RTOS)
	char		name[_IFNAMSIZ];	///< Interface name of this VNIC
	uint64_t	mac;			///< MAC Address. (copied from NIC)
	uint16_t	vlan_proto; 		///< VLAN Protocol
	uint16_t	vlan_tci;   		///< VLAN TCI
	uint16_t	budget;			///< Polling limit
	uint64_t	flags;				///< Flags

	// Buffers
	NICQueue	rx;			///< Rx queue
	NICQueue	tx;			///< Tx queue
	NICQueue	srx;			///< Rx Queue for slowpath
	NICQueue	stx;			///< Tx Queue for slowpath

	// Statistics
	uint64_t	input_bytes;		///< Total input bytes
	uint64_t	input_packets;		///< Total input packets
	uint64_t	input_drop_bytes;	///< Total dropped input bytes
	uint64_t	input_drop_packets;	///< Total dropped input packets
	uint64_t	output_bytes;		///< Total output bytes
	uint64_t	output_packets;		///< Total output packets
	uint64_t	output_drop_bytes;	///< Total dropped output bytes
	uint64_t	output_drop_packets;	///< Total dropped output packets

	uint16_t	padding_head;		///< Leading padding of packet buffer
	uint16_t	padding_tail;		///< Trailing padding of packet buffer

	// Constraint
	uint64_t	rx_bandwidth;		///< Rx threshold
	uint64_t	tx_bandwidth;		///< Tx threshold
	uint64_t	rx_wait;		///< Amount of time to wait to adjust the rx bandwidth
	uint64_t	rx_wait_grace;		///< Amount of time to wait to adjust the rx bandwidth gracefully
	uint64_t	tx_wait;		///< Amount of time to wait to adjust the tx bandwidth
	uint64_t	tx_wait_grace;		///< Amount of time to wait to adjust the tx bandwidth gracefully
	uint64_t	rx_closed;		///< The next receivable frequency
	uint64_t	tx_closed;		///< The next transmittable frequency
} VNIC;

/**
 * Initialize internal timer for VNIC module
 *
 * @param freq_per_sec Timer frequency per seconds
 */
void vnic__init_timer(uint64_t freq_per_sec);

/**
 * Initialize VNIC
 *
 * @param vnic Virtual NIC
 * @param attrs Attributes used to initialize the VNIC.
 * This array must have the required attributes and end with VNIC_NONE
 *
 * @return true if initialization succeeded
 */
bool vnic_init(VNIC* vnic, uint64_t* attrs);

/**
 * Generate a new ID for the VNIC
 *
 * @return ID if a new ID is generated. -1 on failure
 */
int vnic_alloc_id();

/**
 * Returns ID
 * @param id ID to return
 */
void vnic_free_id(int id);

/**
 * Allocate packet buffer
 *
 * @param vnic Virtual NIC
 * @param size buffer size
 *
 * @return newly created packet
 */
Packet* vnic_alloc(VNIC* vnic, size_t size);

/**
 * Free packet buffer
 *
 * @param vnic VNIC that owns the packet
 * @param packet Packet created using the vnic_alloc API function
 */
bool vnic_free(VNIC* vnic, Packet* packet);

/**
 * Update the attributes of the VNIC
 *
 * @param nic Virtual NIC
 * @param attrs attributes used to update the VNIC.
 *
 * @return VNIC_ERROR_NOERROR for success, error number for failure
 */
VNICError vnic_update(VNIC* nic, uint64_t* attrs);

// Fastpath Rx/Tx
/**
 * Check if there is received data
 *
 * @param vnic Virtual NIC
 *
 * @return true if VNIC has data
 */
bool vnic_has_rx(VNIC* vnic);

/**
 * Receive a packet data
 * This function is mainly called to pass the packet received from NICDev to the VNIC

 * @param vnic Virtual NIC
 * @param buf1 packet data
 * @param size1 packet data length
 * @param buf2 optional packet data
 * @param size2 optional packet data length
 *
 * @return VNIC_ERROR_NOERROR for success, error number for failure
 */
VNICError vnic_rx(VNIC* vnic, uint8_t* buf1, size_t size1, uint8_t* buf2, size_t size2);

/**
 * Receive a Packet
 * This function is used to exchange data between VNICs
 *
 * @param vnic Virtual NIC
 * @param packet packet
 *
 * @return zero(VNIC_ERROR_NOERROR) for success, nonzero for failure
 */
VNICError vnic_rx2(VNIC* vnic, Packet* packet);

/**
 * Check if there is data available for transmission.
 *
 * @param vnic Virtual NIC
 *
 * @return true if VNIC has data for transmission
 */
bool vnic_has_tx(VNIC* vnic);

/**
 * Sends queued packets
 *
 * @param vnic Virtual NIC
 * @param transmitter Driver function that transmit packets
 * @param transmitter_context Driver function context
 *
 * @return VNIC_ERROR_NOERROR for success,
 * VNIC_ERROR_RESOURCE_NOT_AVAILABLE and VNIC_ERROR_OPERATION_FAILED for failure
 */
VNICError vnic_tx(VNIC* vnic, bool (*transmitter)(Packet*, void*), void* transmitter_context);

// Slowpath Rx/Tx
/**
 * Check if there is received slowpath data
 *
 * @param vnic Virtual NIC
 *
 * @return true if VNIC has slowpath data
 */
bool vnic_has_srx(VNIC* vnic);

/**
 * Receive a slowpath packet data
 * This function is mainly called to pass the packet received from NICDev to the VNIC

 * @param vnic Virtual NIC
 * @param buf1 packet data
 * @param size1 packet data length
 * @param buf2 optional packet data
 * @param size2 optional packet data length
 *
 * @return zero(VNIC_ERROR_NOERROR) for success, nonzero for failure
 */
bool vnic_srx(VNIC* vnic, uint8_t* buf1, size_t size1, uint8_t* buf2, size_t size2);

/**
 * Receive a slowpath packet
 * This function is used to exchange data between VNICs belonging to the same VM
 *
 * @param vnic Virtual NIC
 * @param packet packet
 *
 * @return zero(VNIC_ERROR_NOERROR) for success, nonzero for failure
 */
bool vnic_srx2(VNIC* vnic, Packet* packet);

/**
 * Check if there is slowpath data available for transmission.
 *
 * @param vnic Virtual NIC
 *
 * @return true if VNIC has data for transmission
 */
bool vnic_has_stx(VNIC* vnic);

/**
 * Sends the queued slowpath data to the VNIC
 *
 * @param vnic Virtual NIC
 *
 * @return transmitted packet
 */
VNICError vnic_stx(VNIC* vnic, bool (*transmitter)(Packet*, void*), void* transmitter_context);

#endif /* __VNIC_H__ */
