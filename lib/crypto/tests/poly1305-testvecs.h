/* SPDX-License-Identifier: GPL-2.0-or-later */
/* This file was generated by: ./scripts/crypto/gen-hash-testvecs.py poly1305 */

static const struct {
	size_t data_len;
	u8 digest[POLY1305_DIGEST_SIZE];
} hash_testvecs[] = {
	{
		.data_len = 0,
		.digest = {
			0xe8, 0x2d, 0x67, 0x2c, 0x01, 0x48, 0xf9, 0xb7,
			0x87, 0x85, 0x3f, 0xcf, 0x18, 0x66, 0x8c, 0xd3,
		},
	},
	{
		.data_len = 1,
		.digest = {
			0xb8, 0xad, 0xca, 0x6b, 0x32, 0xba, 0x34, 0x42,
			0x54, 0x10, 0x28, 0xf5, 0x0f, 0x7e, 0x8e, 0xe3,
		},
	},
	{
		.data_len = 2,
		.digest = {
			0xb8, 0xf7, 0xf4, 0xc2, 0x85, 0x33, 0x80, 0x63,
			0xd1, 0x45, 0xda, 0xf8, 0x7c, 0x79, 0x42, 0xd1,
		},
	},
	{
		.data_len = 3,
		.digest = {
			0xf3, 0x73, 0x7b, 0x60, 0x24, 0xcc, 0x5d, 0x3e,
			0xd1, 0x95, 0x86, 0xce, 0x89, 0x0a, 0x33, 0xba,
		},
	},
	{
		.data_len = 16,
		.digest = {
			0x0a, 0x1a, 0x2d, 0x39, 0xea, 0x49, 0x8f, 0xb7,
			0x90, 0xb6, 0x74, 0x3b, 0x41, 0x3b, 0x96, 0x11,
		},
	},
	{
		.data_len = 32,
		.digest = {
			0x99, 0x05, 0xe3, 0xa7, 0x9e, 0x2a, 0xd2, 0x42,
			0xb9, 0x45, 0x0c, 0x08, 0xe7, 0x10, 0xe4, 0xe1,
		},
	},
	{
		.data_len = 48,
		.digest = {
			0xe1, 0xb2, 0x15, 0xee, 0xa2, 0xf3, 0x04, 0xac,
			0xdd, 0x27, 0x57, 0x95, 0x2f, 0x45, 0xa8, 0xd3,
		},
	},
	{
		.data_len = 49,
		.digest = {
			0x1c, 0xf3, 0xab, 0x39, 0xc0, 0x69, 0x49, 0x69,
			0x89, 0x6f, 0x1f, 0x03, 0x16, 0xe7, 0xc0, 0xf0,
		},
	},
	{
		.data_len = 63,
		.digest = {
			0x30, 0xb0, 0x32, 0x87, 0x51, 0x55, 0x9c, 0x39,
			0x38, 0x42, 0x06, 0xe9, 0x2a, 0x3e, 0x2c, 0x92,
		},
	},
	{
		.data_len = 64,
		.digest = {
			0x2c, 0x04, 0x16, 0x36, 0x55, 0x25, 0x2d, 0xc6,
			0x3d, 0x70, 0x5b, 0x88, 0x46, 0xb6, 0x71, 0x77,
		},
	},
	{
		.data_len = 65,
		.digest = {
			0x03, 0x87, 0xdd, 0xbe, 0xe8, 0x30, 0xf2, 0x15,
			0x40, 0x44, 0x29, 0x7b, 0xb1, 0xe9, 0x9d, 0xe7,
		},
	},
	{
		.data_len = 127,
		.digest = {
			0x29, 0x83, 0x4f, 0xcb, 0x5a, 0x93, 0x25, 0xad,
			0x05, 0xa4, 0xb3, 0x24, 0x77, 0x62, 0x2d, 0x3d,
		},
	},
	{
		.data_len = 128,
		.digest = {
			0x20, 0x0e, 0x2c, 0x05, 0xe2, 0x0b, 0x85, 0xa0,
			0x24, 0x73, 0x7f, 0x65, 0x70, 0x6c, 0x3e, 0xb0,
		},
	},
	{
		.data_len = 129,
		.digest = {
			0xef, 0x2f, 0x98, 0x42, 0xc2, 0x90, 0x55, 0xea,
			0xba, 0x28, 0x76, 0xfd, 0x9e, 0x3e, 0x4d, 0x53,
		},
	},
	{
		.data_len = 256,
		.digest = {
			0x9e, 0x75, 0x4b, 0xc7, 0x69, 0x68, 0x51, 0x90,
			0xdc, 0x29, 0xc8, 0xfa, 0x86, 0xf1, 0xc9, 0xb3,
		},
	},
	{
		.data_len = 511,
		.digest = {
			0x9d, 0x13, 0xf5, 0x54, 0xe6, 0xe3, 0x45, 0x38,
			0x8b, 0x6d, 0x5c, 0xc4, 0x50, 0xeb, 0x90, 0xcb,
		},
	},
	{
		.data_len = 513,
		.digest = {
			0xaa, 0xb2, 0x3e, 0x3c, 0x2a, 0xfc, 0x62, 0x0e,
			0xd4, 0xe6, 0xe5, 0x5c, 0x6b, 0x9f, 0x3d, 0xc7,
		},
	},
	{
		.data_len = 1000,
		.digest = {
			0xd6, 0x8c, 0xea, 0x8a, 0x0f, 0x68, 0xa9, 0xa8,
			0x67, 0x86, 0xf9, 0xc1, 0x4c, 0x26, 0x10, 0x6d,
		},
	},
	{
		.data_len = 3333,
		.digest = {
			0xdc, 0xc1, 0x54, 0xe7, 0x38, 0xc6, 0xdb, 0x24,
			0xa7, 0x0b, 0xff, 0xd3, 0x1b, 0x93, 0x01, 0xa6,
		},
	},
	{
		.data_len = 4096,
		.digest = {
			0x8f, 0x88, 0x3e, 0x9c, 0x7b, 0x2e, 0x82, 0x5a,
			0x1d, 0x31, 0x82, 0xcc, 0x69, 0xb4, 0x16, 0x26,
		},
	},
	{
		.data_len = 4128,
		.digest = {
			0x23, 0x45, 0x94, 0xa8, 0x11, 0x54, 0x9d, 0xf2,
			0xa1, 0x9a, 0xca, 0xf9, 0x3e, 0x65, 0x52, 0xfd,
		},
	},
	{
		.data_len = 4160,
		.digest = {
			0x7b, 0xfc, 0xa9, 0x1e, 0x03, 0xad, 0xef, 0x03,
			0xe2, 0x20, 0x92, 0xc7, 0x54, 0x83, 0xfa, 0x37,
		},
	},
	{
		.data_len = 4224,
		.digest = {
			0x46, 0xab, 0x8c, 0x75, 0xb3, 0x10, 0xa6, 0x3f,
			0x74, 0x55, 0x42, 0x6d, 0x69, 0x35, 0xd2, 0xf5,
		},
	},
	{
		.data_len = 16384,
		.digest = {
			0xd0, 0xfe, 0x26, 0xc2, 0xca, 0x94, 0x2d, 0x52,
			0x2d, 0xe1, 0x11, 0xdd, 0x42, 0x28, 0x83, 0xa6,
		},
	},
};

static const u8 hash_testvec_consolidated[POLY1305_DIGEST_SIZE] = {
	0x9d, 0x07, 0x5d, 0xc9, 0x6c, 0xeb, 0x62, 0x5d,
	0x02, 0x5f, 0xe1, 0xe3, 0xd1, 0x71, 0x69, 0x34,
};

static const u8 poly1305_allones_macofmacs[POLY1305_DIGEST_SIZE] = {
	0x0c, 0x26, 0x6b, 0x45, 0x87, 0x06, 0xcf, 0xc4,
	0x3f, 0x70, 0x7d, 0xb3, 0x50, 0xdd, 0x81, 0x25,
};
