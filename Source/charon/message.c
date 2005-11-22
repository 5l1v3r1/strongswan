/**
 * @file message.c
 *
 * @brief Class message_t. Object of this type represents an IKEv2-Message.
 *
 */

/*
 * Copyright (C) 2005 Jan Hutter, Martin Willi
 * Hochschule fuer Technik Rapperswil
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <stdlib.h>

#include "message.h"

#include "types.h"
#include "globals.h"
#include "ike_sa_id.h"
#include "generator.h"
#include "utils/linked_list.h"
#include "utils/allocator.h"
#include "utils/logger_manager.h"
#include "payloads/encodings.h"
#include "payloads/payload.h"
#include "parser.h"


/**
 * Supported payload entry used in message_rule_t
 * 
 */
typedef struct supported_payload_entry_s supported_payload_entry_t;

struct supported_payload_entry_s {
	/**
	 * Payload type
	 */
	 payload_type_t payload_type;
	 
	 /**
	  * Minimal occurence of this payload
	  */
	 size_t min_occurence;

	 /**
	  * Max occurence of this payload
	  */	 
	 size_t max_occurence;
};

/**
 * Message Rule used to find out which payloads are supported by each message type
 * 
 */
typedef struct message_rule_s message_rule_t;

struct message_rule_s {
	/**
	 * Type of message
	 */
	exchange_type_t exchange_type;
	
	/**
	 * Is message a request or response
	 */
	bool is_request;
	 /**
	  * Number of supported payloads
	  */
	 size_t supported_payloads_count;
	/**
	 * Pointer to first supported payload entry
	 */
	 supported_payload_entry_t *supported_payloads;
};

/**
 * message rule for ike_sa_init from initiator
 */
static supported_payload_entry_t supported_ike_sa_init_i_payloads[] =
{
	{SECURITY_ASSOCIATION,1,1},
	{KEY_EXCHANGE,1,1},
	{NONCE,1,1},
};

/**
 * message rule for ike_sa_init from responder
 */
static supported_payload_entry_t supported_ike_sa_init_r_payloads[] =
{
	{SECURITY_ASSOCIATION,1,1},
	{KEY_EXCHANGE,1,1},
	{NONCE,1,1},
};


/**
 * message rules, defines allowed payloads
 */
static message_rule_t message_rules[] = {
	{IKE_SA_INIT,TRUE,(sizeof(supported_ike_sa_init_i_payloads)/sizeof(supported_payload_entry_t)),supported_ike_sa_init_i_payloads},
	{IKE_SA_INIT,FALSE,(sizeof(supported_ike_sa_init_r_payloads)/sizeof(supported_payload_entry_t)),supported_ike_sa_init_r_payloads}
};

/**
 * Entry for a payload in the internal used linked list
 * 
 */
typedef struct payload_entry_s payload_entry_t;

struct payload_entry_s {
	/**
	 * Type of payload
	 */
	payload_type_t payload_type;
	/**
	 * Data struct holding the data of given payload
	 */
	void *data_struct;
};


/**
 * Private data of an message_t object
 */
typedef struct private_message_s private_message_t;

struct private_message_s {

	/**
	 * Public part of a message_t object
	 */
	message_t public;


	/**
	 * Minor version of message
	 */
	u_int8_t major_version;
	
	/**
	 * Major version of message
	 */
	u_int8_t minor_version;
	
	/**
	 * First Payload in message
	 */
	payload_type_t first_payload;

	/**
	 * Assigned exchange type
	 */
	exchange_type_t exchange_type;


	/**
	 * TRUE if message is request.
	 * FALSE if message is reply.
	 */
	bool is_request;
	
	/**
	 * Message ID of this message
	 */
	u_int32_t message_id;
	
	/**
	 * ID of assigned IKE_SA
	 */
	ike_sa_id_t *ike_sa_id;
	
	/**
	 * Assigned UDP packet.
	 * 
	 * Stores incoming packet or last generated one.
	 */
	packet_t *packet;
	 
	/**
	 * Linked List where payload data are stored in
	 */
	linked_list_t *payloads;
	
	 /**
	  * Assigned parser to parse Header and Body of this message
	  */
	parser_t *parser;
	
	/**
	 * logger for this message
	 */
	logger_t *logger;
	
	/**
	 * Gets a list of supported payloads of this message type
	 * 
	 * @param this							calling object
	 * @param[out] supported_payloads		first entry of supported payloads
	 * @param[out] supported_payloads_count	number of supported payload entries
	 * 
	 * @return 								SUCCESS
	 * 										NOT_FOUND if no supported payload definition could be found
	 */
	status_t (*get_supported_payloads) (private_message_t *this, supported_payload_entry_t **supported_payloads,size_t *supported_payloads_count);
	
};

/**
 * Implements private_message_t's get_supported_payloads function.
 * See #private_message_t.get_supported_payloads.
 */
status_t get_supported_payloads (private_message_t *this, supported_payload_entry_t **supported_payloads,size_t *supported_payloads_count)
{
	int i;
	exchange_type_t exchange_type = this->public.get_exchange_type(&(this->public));
	bool is_request = this->public.get_request(&(this->public));
	
	
	for (i = 0; i < (sizeof(message_rules) / sizeof(message_rule_t)); i++)
	{
		if ((exchange_type == message_rules[i].exchange_type) &&
			(is_request == message_rules[i].is_request))
		{
			/* found rule for given exchange_type*/
			*supported_payloads = message_rules[i].supported_payloads;
			*supported_payloads_count = message_rules[i].supported_payloads_count;
			
			return SUCCESS;
		}
		
		
	}
	*supported_payloads = NULL;
	*supported_payloads_count = 0;
	return NOT_FOUND;
}

/**
 * Implements message_t's set_ike_sa_id function.
 * See #message_s.set_ike_sa_id.
 */
static status_t set_ike_sa_id (private_message_t *this,ike_sa_id_t *ike_sa_id)
{
	status_t status;
	status = ike_sa_id->clone(ike_sa_id,&(this->ike_sa_id));
	return status;
}

/**
 * Implements message_t's get_ike_sa_id function.
 * See #message_s.get_ike_sa_id.
 */
static status_t get_ike_sa_id (private_message_t *this,ike_sa_id_t **ike_sa_id)
{
	status_t status;
	if (this->ike_sa_id == NULL)
	{
		return FAILED;
	}
	status = this->ike_sa_id->clone(this->ike_sa_id,ike_sa_id);
	return status;
}


/**
 * Implements message_t's set_message_id function.
 * See #message_s.set_message_id.
 */
static status_t set_message_id (private_message_t *this,u_int32_t message_id)
{
	this->message_id = message_id;
	return SUCCESS;
}


/**
 * Implements message_t's set_message_id function.
 * See #message_s.set_message_id.
 */
static u_int32_t get_message_id (private_message_t *this)
{
	return this->message_id;
}

/**
 * Implements message_t's set_major_version function.
 * See #message_s.set_major_version.
 */
static status_t set_major_version (private_message_t *this,u_int8_t major_version)
{
	this->major_version = major_version;
	return SUCCESS;
}


/**
 * Implements message_t's get_major_version function.
 * See #message_s.get_major_version.
 */
static u_int8_t get_major_version (private_message_t *this)
{
	return this->major_version;
}

/**
 * Implements message_t's set_minor_version function.
 * See #message_s.set_minor_version.
 */
static status_t set_minor_version (private_message_t *this,u_int8_t minor_version)
{
	this->minor_version = minor_version;
	return SUCCESS;
}


/**
 * Implements message_t's get_minor_version function.
 * See #message_s.get_minor_version.
 */
static u_int8_t get_minor_version (private_message_t *this)
{
	return this->minor_version;
}

/**
 * Implements message_t's set_exchange_type function.
 * See #message_s.set_exchange_type.
 */
static status_t set_exchange_type (private_message_t *this,exchange_type_t exchange_type)
{
	this->exchange_type = exchange_type;
	return SUCCESS;
}


/**
 * Implements message_t's get_exchange_type function.
 * See #message_s.get_exchange_type.
 */
static exchange_type_t get_exchange_type (private_message_t *this)
{
	return this->exchange_type;
}


/**
 * Implements message_t's set_request function.
 * See #message_s.set_request.
 */
static status_t set_request (private_message_t *this,bool request)
{
	this->is_request = request;
	return SUCCESS;
}

/**
 * Implements message_t's get_request function.
 * See #message_s.get_request.
 */
static exchange_type_t get_request (private_message_t *this)
{
	return this->is_request;
}

static status_t add_payload(private_message_t *this, payload_t *payload)
{
	payload_t *last_payload;
	if ((this->payloads->get_count(this->payloads) > 0) &&
	    (this->payloads->get_last(this->payloads,(void **) &last_payload) != SUCCESS))
	{
		return OUT_OF_RES;	
	}
	
	if (this->payloads->insert_last(this->payloads, payload) != SUCCESS)
	{
		return OUT_OF_RES;	
	}
	if (this->payloads->get_count(this->payloads) == 1)
	{
		this->first_payload = payload->get_type(payload);
	}
	else
	{
		last_payload->set_next_type(last_payload,payload->get_type(payload));
	}
	
	this->logger->log(this->logger, CONTROL|MORE, "added payload of type %s to message", 
						mapping_find(payload_type_m, payload->get_type(payload)));
	
	return SUCCESS;
}

static status_t set_source(private_message_t *this, host_t *host)
{
	if (this->packet->source != NULL)
	{
		this->packet->source->destroy(this->packet->source);	
	}
	this->packet->source = host;
	return SUCCESS;
}

static status_t set_destination(private_message_t *this, host_t *host)
{
	if (this->packet->destination != NULL)
	{
		this->packet->destination->destroy(this->packet->destination);	
	}
	this->packet->destination = host;
	return SUCCESS;
}

static status_t get_source(private_message_t *this, host_t **host)
{
	*host = this->packet->source;
	return SUCCESS;
}

static status_t get_destination(private_message_t *this, host_t **host)
{
	*host = this->packet->destination;
	return SUCCESS;
}


static status_t get_payload_iterator(private_message_t *this, linked_list_iterator_t **iterator)
{
	return this->payloads->create_iterator(this->payloads, iterator, TRUE);
}


/**
 * Implements message_t's generate function.
 * See #message_s.generate.
 */
static status_t generate(private_message_t *this, packet_t **packet)
{
	generator_t *generator;
	ike_header_t *ike_header;
	payload_t *payload, *next_payload;
	linked_list_iterator_t *iterator;
	status_t status;
	
	
	this->logger->log(this->logger, CONTROL, "generating message, contains %d payloads", 
						this->payloads->get_count(this->payloads));
	
	if (this->exchange_type == EXCHANGE_TYPE_UNDEFINED)
	{
		this->logger->log(this->logger, ERROR, "exchange type is not defined");
		return INVALID_STATE;
	}
	
	if (this->packet->source == NULL ||
		this->packet->destination == NULL) 
	{
		this->logger->log(this->logger, ERROR, "source/destination not defined");
		return INVALID_STATE;
	}
	
	
	ike_header = ike_header_create();
	if (ike_header == NULL)
	{
		return OUT_OF_RES;
	}
	
	
	ike_header->set_exchange_type(ike_header, this->exchange_type);
	ike_header->set_message_id(ike_header, this->message_id);
	ike_header->set_response_flag(ike_header, !this->is_request);
	ike_header->set_initiator_flag(ike_header, this->ike_sa_id->is_initiator(this->ike_sa_id));
	ike_header->set_initiator_spi(ike_header, this->ike_sa_id->get_initiator_spi(this->ike_sa_id));
	ike_header->set_responder_spi(ike_header, this->ike_sa_id->get_responder_spi(this->ike_sa_id));
	
	generator = generator_create();
	if (generator == NULL)
	{
		return OUT_OF_RES;	
	}
	
	payload = (payload_t*)ike_header;

	if (this->payloads->create_iterator(this->payloads, &iterator, TRUE) != SUCCESS)
	{
		generator->destroy(generator);
		ike_header->destroy(ike_header);
		return OUT_OF_RES;
	}
	while(iterator->has_next(iterator))
	{
		iterator->current(iterator, (void**)&next_payload);
		payload->set_next_type(payload, next_payload->get_type(next_payload));
		status = generator->generate_payload(generator, payload);
		if (status != SUCCESS)
		{
			generator->destroy(generator);
			ike_header->destroy(ike_header);
			return status;
		}
		payload = next_payload;
	}
	iterator->destroy(iterator);
	
	payload->set_next_type(payload, NO_PAYLOAD);
	status = generator->generate_payload(generator, payload);
	if (status != SUCCESS)
	{
		generator->destroy(generator);
		ike_header->destroy(ike_header);
		return status;
	}
	
	ike_header->destroy(ike_header);
	
	
	
	if (this->packet->data.ptr != NULL)
	{
		allocator_free(this->packet->data.ptr);
	}	
	
	status = generator->write_to_chunk(generator, &(this->packet->data));
	if (status != SUCCESS)
	{
		generator->destroy(generator);
		return status;
	}
	
	this->packet->clone(this->packet, packet);
	
	generator->destroy(generator);
	
	
	this->logger->log(this->logger, CONTROL, "message generated successfully");
	return SUCCESS;
}

/**
 * Implements message_t's parse_header function.
 * See #message_s.parse_header.
 */
static status_t parse_header(private_message_t *this)
{
	ike_header_t *ike_header;
	status_t status;
	
	
	this->logger->log(this->logger, CONTROL, "parsing header of message");
	
	this->parser->reset_context(this->parser);
	status = this->parser->parse_payload(this->parser,HEADER,(payload_t **) &ike_header);
	if (status != SUCCESS)
	{
		this->logger->log(this->logger, ERROR, "Header could not be parsed");
		return status;
		
	}
	
	/* verify payload */
	status = ike_header->payload_interface.verify(&(ike_header->payload_interface));
	if (status != SUCCESS)
	{
		this->logger->log(this->logger, ERROR, "Header verification failed");
		ike_header->destroy(ike_header);
		return status;
	}	
	
	if (this->ike_sa_id != NULL)
	{
		this->ike_sa_id->destroy(this->ike_sa_id);
	}
	
	this->ike_sa_id = ike_sa_id_create(ike_header->get_initiator_spi(ike_header),
									   ike_header->get_responder_spi(ike_header),
									   ike_header->get_initiator_flag(ike_header));
	if (this->ike_sa_id == NULL)
	{
		this->logger->log(this->logger, ERROR, "could not create ike_sa_id object");
		ike_header->destroy(ike_header);
		return OUT_OF_RES;
	}
	this->exchange_type = ike_header->get_exchange_type(ike_header);
	this->message_id = ike_header->get_message_id(ike_header);
	this->is_request = (!(ike_header->get_response_flag(ike_header)));
	this->major_version = ike_header->get_maj_version(ike_header);
	this->minor_version = ike_header->get_min_version(ike_header);
	this->first_payload = ike_header->payload_interface.get_next_type(&(ike_header->payload_interface));
	
	
	this->logger->log(this->logger, CONTROL, "parsing header successfully");
	
	ike_header->destroy(ike_header);	
	return SUCCESS;	
}

/**
 * Implements message_t's parse_body function.
 * See #message_s.parse_body.
 */
static status_t parse_body (private_message_t *this)
{
	status_t status = SUCCESS;
	int i;
	payload_type_t current_payload_type = this->first_payload;
	supported_payload_entry_t *supported_payloads;
	size_t supported_payloads_count;
	
	
	this->logger->log(this->logger, CONTROL, "parsing body of message");
			
	if (this->get_supported_payloads (this, &supported_payloads, &supported_payloads_count) != SUCCESS)
	{
		this->logger->log(this->logger, ERROR, "could not get supported payloads");
		return FAILED;
	}
		
	while (current_payload_type != NO_PAYLOAD)
	{
		payload_t *current_payload;
		bool supported = FALSE;
		
		this->logger->log(this->logger, CONTROL|MORE, "start parsing payload of type %s", 
							mapping_find(payload_type_m, current_payload_type));
		for (i = 0; i < supported_payloads_count;i++)
		{
			if (supported_payloads[i].payload_type == current_payload_type)
			{
				supported = TRUE;
				break;
			}
		}
		if (!supported && (current_payload_type != NO_PAYLOAD))
		{
			/* type not supported */
			status = NOT_SUPPORTED;
			this->logger->log(this->logger, ERROR, "payload type %s not supported",mapping_find(payload_type_m,current_payload_type));
			break;
		}
		
		status = this->parser->parse_payload(this->parser,current_payload_type,(payload_t **) &current_payload);
		if (status != SUCCESS)
		{
			this->logger->log(this->logger, ERROR, "payload type %s could not be parsed",mapping_find(payload_type_m,current_payload_type));			
			break;
		}
		
		status = current_payload->verify(current_payload);
		if (status != SUCCESS)
		{
			this->logger->log(this->logger, ERROR, "payload type %s could not be verified",mapping_find(payload_type_m,current_payload_type));			
			status = VERIFY_ERROR;
			break;
		}

		/* get next payload type */
		current_payload_type = current_payload->get_next_type(current_payload);
		
		status = this->payloads->insert_last(this->payloads,current_payload);
		if (status != SUCCESS)
		{
			this->logger->log(this->logger, ERROR, "Could not insert current payload to internal list cause of ressource exhausting");
			break;			
		}
		
	}
	if (status != SUCCESS)
	{
		/* already parsed payload is destroyed later in destroy call from outside this object */
	}
	else
	{
		linked_list_iterator_t *iterator;
		
		status = this->payloads->create_iterator(this->payloads,&iterator,TRUE);
		if (status != SUCCESS)
		{
			this->logger->log(this->logger, ERROR, "Could not create iterator to check supported payloads");
			return status;
		}
		
		/* check for payloads with wrong count*/
		for (i = 0; i < supported_payloads_count;i++)
		{
			size_t min_occurence = supported_payloads[i].min_occurence;
			size_t max_occurence = supported_payloads[i].max_occurence;
			payload_type_t payload_type = supported_payloads[i].payload_type;
			size_t found_payloads = 0;
	
			iterator->reset(iterator);
			
			while(iterator->has_next(iterator))
			{
				payload_t *current_payload;
				status = iterator->current(iterator,(void **)&current_payload);
				if (status != SUCCESS)
				{
					this->logger->log(this->logger, ERROR, "Could not get payload from internal list");
					iterator->destroy(iterator);
					return status;
				}
				if (current_payload->get_type(current_payload) == payload_type)
				{
					found_payloads++;
					if (found_payloads > max_occurence)
					{
						this->logger->log(this->logger, ERROR, "Payload of type %s more than %d times (%d) occured in current message",
										  mapping_find(payload_type_m,current_payload->get_type(current_payload)),max_occurence,found_payloads);
						iterator->destroy(iterator);
						return NOT_SUPPORTED;					
					}
				}

			}
			if (found_payloads < min_occurence)
			{
					this->logger->log(this->logger, ERROR, "Payload of type %s not occured %d times",
									  mapping_find(payload_type_m,payload_type),min_occurence);
					iterator->destroy(iterator);
					return NOT_SUPPORTED;
			}
			
		}
		iterator->destroy(iterator);
	}
	return status;	
}



/**
 * Implements message_t's destroy function.
 * See #message_s.destroy.
 */
static status_t destroy (private_message_t *this)
{
	linked_list_iterator_t *iterator;
	
	this->packet->destroy(this->packet);

	if (this->ike_sa_id != NULL)
	{
		this->ike_sa_id->destroy(this->ike_sa_id);
	}
	
	this->payloads->create_iterator(this->payloads, &iterator, TRUE);
	while (iterator->has_next(iterator))
	{
		payload_t *payload;
		iterator->current(iterator, (void**)&payload);	
		this->logger->log(this->logger, CONTROL|MOST, "Destroying payload of type %s", 
							mapping_find(payload_type_m, payload->get_type(payload)));
		payload->destroy(payload);
	}
	iterator->destroy(iterator);
	this->payloads->destroy(this->payloads);
	this->parser->destroy(this->parser);
	global_logger_manager->destroy_logger(global_logger_manager, this->logger);
	
	allocator_free(this);
	return SUCCESS;
}

/*
 * Described in Header-File
 */
message_t *message_create_from_packet(packet_t *packet)
{
	private_message_t *this = allocator_alloc_thing(private_message_t);
	if (this == NULL)
	{
		return NULL;
	}

	/* public functions */
	this->public.set_major_version = (status_t(*)(message_t*, u_int8_t))set_major_version;
	this->public.get_major_version = (u_int8_t(*)(message_t*))get_major_version;
	this->public.set_minor_version = (status_t(*)(message_t*, u_int8_t))set_minor_version;
	this->public.get_minor_version = (u_int8_t(*)(message_t*))get_minor_version;
	this->public.set_message_id = (status_t(*)(message_t*, u_int32_t))set_message_id;
	this->public.get_message_id = (u_int32_t(*)(message_t*))get_message_id;
	this->public.set_ike_sa_id = (status_t(*)(message_t*, ike_sa_id_t *))set_ike_sa_id;
	this->public.get_ike_sa_id = (status_t(*)(message_t*, ike_sa_id_t **))get_ike_sa_id;
	this->public.set_exchange_type = (status_t(*)(message_t*, exchange_type_t))set_exchange_type;
	this->public.get_exchange_type = (exchange_type_t(*)(message_t*))get_exchange_type;
	this->public.set_request = (status_t(*)(message_t*, bool))set_request;
	this->public.get_request = (bool(*)(message_t*))get_request;
	this->public.add_payload = (status_t(*)(message_t*,payload_t*))add_payload;
	this->public.generate = (status_t (*) (message_t *, packet_t**)) generate;
	this->public.set_source = (status_t (*) (message_t*,host_t*)) set_source;
	this->public.get_source = (status_t (*) (message_t*,host_t**)) get_source;
	this->public.set_destination = (status_t (*) (message_t*,host_t*)) set_destination;
	this->public.get_destination = (status_t (*) (message_t*,host_t**)) get_destination;
	this->public.get_payload_iterator = (status_t (*) (message_t *, linked_list_iterator_t **)) get_payload_iterator;
	this->public.parse_header = 	(status_t (*) (message_t *)) parse_header;
	this->public.parse_body = 	(status_t (*) (message_t *)) parse_body;
	this->public.destroy = (status_t(*)(message_t*))destroy;
		
	/* public values */
	this->exchange_type = EXCHANGE_TYPE_UNDEFINED;
 	this->is_request = TRUE;
 	this->ike_sa_id = NULL;
 	this->first_payload = NO_PAYLOAD;
 	this->message_id = 0;

	/* private functions */
	this->get_supported_payloads = get_supported_payloads;

	/* private values */
	if (packet == NULL)
	{
		packet = packet_create();	
	}
	if (packet == NULL)
	{
		allocator_free(this);
		return NULL;
	}
	this->packet = packet;
	this->payloads = linked_list_create();
	if (this->payloads == NULL)
	{
		allocator_free(this);
		return NULL;
	}
	
	/* parser is created from data of packet */
 	this->parser = parser_create(this->packet->data);
 	if (this->parser == NULL)
 	{
 		this->payloads->destroy(this->payloads);
		allocator_free(this);
		return NULL;
 	}
		
	this->logger = global_logger_manager->create_logger(global_logger_manager, MESSAGE, NULL);
	if (this->logger == NULL)
	{
		this->parser->destroy(this->parser);
		this->payloads->destroy(this->payloads);
		allocator_free(this);
		return NULL;
	}

	return (&this->public);
}

/*
 * Described in Header-File
 */
message_t *message_create()
{
	return message_create_from_packet(NULL);
}
