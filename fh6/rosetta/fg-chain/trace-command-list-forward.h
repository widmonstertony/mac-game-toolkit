// Public ID3D12GraphicsCommandList forwarding declarations, generated from
// the installed MinGW D3D12 interface declarations; no runtime object patching.
 HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *data_size, void *data) override {return real->GetPrivateData(guid,data_size,data);}
 HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT data_size, const void *data) override {return real->SetPrivateData(guid,data_size,data);}
 HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *data) override {return real->SetPrivateDataInterface(guid,data);}
 HRESULT STDMETHODCALLTYPE SetName(const WCHAR *name) override {return real->SetName(name);}
 HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **device) override {return real->GetDevice(riid,device);}
 D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE GetType() override {return real->GetType();}
 HRESULT STDMETHODCALLTYPE Close() override {return real->Close();}
 HRESULT STDMETHODCALLTYPE Reset(ID3D12CommandAllocator *allocator, ID3D12PipelineState *initial_state) override {return real->Reset(allocator,initial_state);}
 void STDMETHODCALLTYPE ClearState(ID3D12PipelineState *pipeline_state) override {return real->ClearState(pipeline_state);}
 void STDMETHODCALLTYPE CopyBufferRegion(ID3D12Resource *dst_buffer, UINT64 dst_offset, ID3D12Resource *src_buffer, UINT64 src_offset, UINT64 byte_count) override {return real->CopyBufferRegion(dst_buffer,dst_offset,src_buffer,src_offset,byte_count);}
 void STDMETHODCALLTYPE CopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION *dst, UINT dst_x, UINT dst_y, UINT dst_z, const D3D12_TEXTURE_COPY_LOCATION *src, const D3D12_BOX *src_box) override {return real->CopyTextureRegion(dst,dst_x,dst_y,dst_z,src,src_box);}
 void STDMETHODCALLTYPE CopyResource(ID3D12Resource *dst_resource, ID3D12Resource *src_resource) override {return real->CopyResource(dst_resource,src_resource);}
 void STDMETHODCALLTYPE CopyTiles(ID3D12Resource *tiled_resource, const D3D12_TILED_RESOURCE_COORDINATE *tile_region_start_coordinate, const D3D12_TILE_REGION_SIZE *tile_region_size, ID3D12Resource *buffer, UINT64 buffer_offset, D3D12_TILE_COPY_FLAGS flags) override {return real->CopyTiles(tiled_resource,tile_region_start_coordinate,tile_region_size,buffer,buffer_offset,flags);}
 void STDMETHODCALLTYPE ResolveSubresource(ID3D12Resource *dst_resource, UINT dst_sub_resource, ID3D12Resource *src_resource, UINT src_sub_resource, DXGI_FORMAT format) override {return real->ResolveSubresource(dst_resource,dst_sub_resource,src_resource,src_sub_resource,format);}
 void STDMETHODCALLTYPE IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY primitive_topology) override {return real->IASetPrimitiveTopology(primitive_topology);}
 void STDMETHODCALLTYPE RSSetViewports(UINT viewport_count, const D3D12_VIEWPORT *viewports) override {return real->RSSetViewports(viewport_count,viewports);}
 void STDMETHODCALLTYPE RSSetScissorRects(UINT rect_count, const D3D12_RECT *rects) override {return real->RSSetScissorRects(rect_count,rects);}
 void STDMETHODCALLTYPE OMSetBlendFactor(const FLOAT blend_factor[4]) override {return real->OMSetBlendFactor(blend_factor);}
 void STDMETHODCALLTYPE OMSetStencilRef(UINT stencil_ref) override {return real->OMSetStencilRef(stencil_ref);}
 void STDMETHODCALLTYPE ExecuteBundle(ID3D12GraphicsCommandList *command_list) override {return real->ExecuteBundle(command_list);}
 void STDMETHODCALLTYPE SetDescriptorHeaps(UINT heap_count, ID3D12DescriptorHeap *const *heaps) override {return real->SetDescriptorHeaps(heap_count,heaps);}
 void STDMETHODCALLTYPE SetComputeRootSignature(ID3D12RootSignature *root_signature) override {return real->SetComputeRootSignature(root_signature);}
 void STDMETHODCALLTYPE SetGraphicsRootSignature(ID3D12RootSignature *root_signature) override {return real->SetGraphicsRootSignature(root_signature);}
 void STDMETHODCALLTYPE SetComputeRootDescriptorTable(UINT root_parameter_index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor) override {return real->SetComputeRootDescriptorTable(root_parameter_index,base_descriptor);}
 void STDMETHODCALLTYPE SetGraphicsRootDescriptorTable(UINT root_parameter_index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor) override {return real->SetGraphicsRootDescriptorTable(root_parameter_index,base_descriptor);}
 void STDMETHODCALLTYPE SetComputeRoot32BitConstant(UINT root_parameter_index, UINT data, UINT dst_offset) override {return real->SetComputeRoot32BitConstant(root_parameter_index,data,dst_offset);}
 void STDMETHODCALLTYPE SetGraphicsRoot32BitConstant(UINT root_parameter_index, UINT data, UINT dst_offset) override {return real->SetGraphicsRoot32BitConstant(root_parameter_index,data,dst_offset);}
 void STDMETHODCALLTYPE SetComputeRoot32BitConstants(UINT root_parameter_index, UINT constant_count, const void *data, UINT dst_offset) override {return real->SetComputeRoot32BitConstants(root_parameter_index,constant_count,data,dst_offset);}
 void STDMETHODCALLTYPE SetGraphicsRoot32BitConstants(UINT root_parameter_index, UINT constant_count, const void *data, UINT dst_offset) override {return real->SetGraphicsRoot32BitConstants(root_parameter_index,constant_count,data,dst_offset);}
 void STDMETHODCALLTYPE SetGraphicsRootConstantBufferView(UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address) override {return real->SetGraphicsRootConstantBufferView(root_parameter_index,address);}
 void STDMETHODCALLTYPE SetComputeRootShaderResourceView(UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address) override {return real->SetComputeRootShaderResourceView(root_parameter_index,address);}
 void STDMETHODCALLTYPE SetGraphicsRootShaderResourceView(UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address) override {return real->SetGraphicsRootShaderResourceView(root_parameter_index,address);}
 void STDMETHODCALLTYPE SetComputeRootUnorderedAccessView(UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address) override {return real->SetComputeRootUnorderedAccessView(root_parameter_index,address);}
 void STDMETHODCALLTYPE SetGraphicsRootUnorderedAccessView(UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address) override {return real->SetGraphicsRootUnorderedAccessView(root_parameter_index,address);}
 void STDMETHODCALLTYPE IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW *view) override {return real->IASetIndexBuffer(view);}
 void STDMETHODCALLTYPE IASetVertexBuffers(UINT start_slot, UINT view_count, const D3D12_VERTEX_BUFFER_VIEW *views) override {return real->IASetVertexBuffers(start_slot,view_count,views);}
 void STDMETHODCALLTYPE SOSetTargets(UINT start_slot, UINT view_count, const D3D12_STREAM_OUTPUT_BUFFER_VIEW *views) override {return real->SOSetTargets(start_slot,view_count,views);}
 void STDMETHODCALLTYPE OMSetRenderTargets(UINT render_target_descriptor_count, const D3D12_CPU_DESCRIPTOR_HANDLE *render_target_descriptors, WINBOOL single_descriptor_handle, const D3D12_CPU_DESCRIPTOR_HANDLE *depth_stencil_descriptor) override {return real->OMSetRenderTargets(render_target_descriptor_count,render_target_descriptors,single_descriptor_handle,depth_stencil_descriptor);}
 void STDMETHODCALLTYPE ClearDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE dsv, D3D12_CLEAR_FLAGS flags, FLOAT depth, UINT8 stencil, UINT rect_count, const D3D12_RECT *rects) override {return real->ClearDepthStencilView(dsv,flags,depth,stencil,rect_count,rects);}
 void STDMETHODCALLTYPE ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const FLOAT color[4], UINT rect_count, const D3D12_RECT *rects) override {return real->ClearRenderTargetView(rtv,color,rect_count,rects);}
 void STDMETHODCALLTYPE ClearUnorderedAccessViewUint(D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, ID3D12Resource *resource, const UINT values[4], UINT rect_count, const D3D12_RECT *rects) override {return real->ClearUnorderedAccessViewUint(gpu_handle,cpu_handle,resource,values,rect_count,rects);}
 void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat(D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, ID3D12Resource *resource, const float values[4], UINT rect_count, const D3D12_RECT *rects) override {return real->ClearUnorderedAccessViewFloat(gpu_handle,cpu_handle,resource,values,rect_count,rects);}
 void STDMETHODCALLTYPE DiscardResource(ID3D12Resource *resource, const D3D12_DISCARD_REGION *region) override {return real->DiscardResource(resource,region);}
 void STDMETHODCALLTYPE BeginQuery(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index) override {return real->BeginQuery(heap,type,index);}
 void STDMETHODCALLTYPE EndQuery(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index) override {return real->EndQuery(heap,type,index);}
 void STDMETHODCALLTYPE ResolveQueryData(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT start_index, UINT query_count, ID3D12Resource *dst_buffer, UINT64 aligned_dst_buffer_offset) override {return real->ResolveQueryData(heap,type,start_index,query_count,dst_buffer,aligned_dst_buffer_offset);}
 void STDMETHODCALLTYPE SetPredication(ID3D12Resource *buffer, UINT64 aligned_buffer_offset, D3D12_PREDICATION_OP operation) override {return real->SetPredication(buffer,aligned_buffer_offset,operation);}
 void STDMETHODCALLTYPE SetMarker(UINT metadata, const void *data, UINT size) override {return real->SetMarker(metadata,data,size);}
 void STDMETHODCALLTYPE BeginEvent(UINT metadata, const void *data, UINT size) override {return real->BeginEvent(metadata,data,size);}
 void STDMETHODCALLTYPE EndEvent() override {return real->EndEvent();}
