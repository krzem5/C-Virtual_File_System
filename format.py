# disk structure:
#   root: {node_descriptor:node_head, node_descriptor:free_node_head, used_sector_descriptor:used}
#   node_descriptor: {offset:first_block_offset, count:block_count, usage_bitmap: [...], node_descriptor:next, node_descriptor:prev}
#   used_sector_descriptor: {offset:first_block_offset, count:block_count, used_sector_descriptor: next, used_sector_descriptor:prev}


root_t={
	"node_descriptor_table": "node_descriptor_table_t*",
	"sector_descriptor_table": "node_descriptor_table_t*"
}



node_descriptor_table_t={ # 2 blocks = 8192 bytes
	"prev": "node_descriptor_table_t*",
	"next": "node_descriptor_table_t*",
	"data": "node_descriptor_array_t*[507]",
	"bitmap": "uint64_t[507]"
}



node_descriptor_array_t={ # 1 block = 4096 bytes
	"data": "node_t[64]"
}



node_t={ # 64 bytes
	"ref_cnt": "uint32_t",
	"name_length": "uint16_t",
	"type": "uint8_t",
	"name": { # union
		"direct": "char[17]", # no terminating 'null'
		"indirect": "char*"
	},
	"parent": "node_t*",
	"next_sibling": "node_t*",
	"prev_sibling": "node_t*",
	"value": { # union
		"data": {
			"start_block_address": "uint64_t",
			"block_count": "uint64_t"
		},
		"link": {
			"link": "void*",
		},
		"directory": {
			"first_child": "node_t*",
		}
	}
}



sector_descriptor_table={

}
