/*
 * Copyright (C) 2012 Reto Buerki
 * Copyright (C) 2012 Adrian-Ken Rueegsegger
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

#ifndef TKM_DIFFIE_HELLMAN_H_
#define TKM_DIFFIE_HELLMAN_H_

typedef struct tkm_diffie_hellman_t tkm_diffie_hellman_t;

#include <library.h>

/**
 * diffie_hellman_t implementation using the trusted key manager.
 */
struct tkm_diffie_hellman_t {

	/**
	 * Implements diffie_hellman_t interface.
	 */
	diffie_hellman_t dh;
};

/**
 * Creates a new tkm_diffie_hellman_t object.
 *
 * @param group			Diffie Hellman group number to use
 * @return				tkm_diffie_hellman_t object, NULL if not supported
 */
tkm_diffie_hellman_t *tkm_diffie_hellman_create(diffie_hellman_group_t group);

#endif /** TKM_DIFFIE_HELLMAN_H_ */
