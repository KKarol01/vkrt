#include <eng/common/to_string.hpp>

#include <fmt/format.h>
#include <eng/common/logger.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/submit_queue.hpp>

namespace eng
{
namespace gfx
{
// clang-format off
std::string to_string(const ImageFormat& a) 
{ 
    switch(a) 
    {
        case ImageFormat::R8G8B8A8_UNORM: { return "R8G8B8A8_UNORM"; }
        case ImageFormat::R8G8B8A8_SRGB: { return "R8G8B8A8_SRGB"; }
        default: { ENG_ERROR("Unhandled case"); return ""; }
    }
}

std::string to_string(const ImageType& a) 
{ 
    switch(a) 
    {
        case ImageType::TYPE_1D: { return "TYPE_1D"; }
        case ImageType::TYPE_2D: { return "TYPE_2D"; }
        case ImageType::TYPE_3D: { return "TYPE_3D"; }
        default: { ENG_ERROR("Unhandled case"); return ""; }
    }
}

std::string to_string(const ImageViewType& a) 
{ 
    switch(a) 
    {
        case ImageViewType::NONE:    { return "NONE"; }
        case ImageViewType::TYPE_1D: { return "TYPE_1D"; }
        case ImageViewType::TYPE_2D: { return "TYPE_2D"; }
        case ImageViewType::TYPE_3D: { return "TYPE_3D"; }
        default: { ENG_ERROR("Unhandled case"); return ""; }
    }
}

std::string to_string(const ImageLayout & a)
{
    switch(a) 
    {
		case ImageLayout::UNDEFINED:	{ return "UNDEFINED"; }
		case ImageLayout::GENERAL:		{ return "GENERAL"; }
		case ImageLayout::READ_ONLY:	{ return "READ_ONLY"; }
		case ImageLayout::ATTACHMENT:	{ return "ATTACHMENT"; }
		case ImageLayout::TRANSFER_SRC:	{ return "TRANSFER_SRC"; }
		case ImageLayout::TRANSFER_DST:	{ return "TRANSFER_DST"; }
		case ImageLayout::PRESENT:		{ return "PRESENT"; }
		default: { return "UNHANDLED_CASE"; }
	}
}

std::string to_string(const SyncType& a)
{
    switch(a) 
    {
        case SyncType::UNKNOWN: { return "UNKNOWN"; }
        case SyncType::FENCE: { return "FENCE"; }
        case SyncType::BINARY_SEMAPHORE: { return "BINARY_SEMAPHORE"; }
        case SyncType::TIMELINE_SEMAPHORE: { return "TIMELINE_SEMAPHORE"; }
        default: { ENG_ERROR("Unhandled case"); return ""; }
    }
}

std::string to_string(const RenderPassType& a)
{
    switch(a)
    {
        case RenderPassType::Z_PREPASS: { return "Z_PREPASS"; }
        case RenderPassType::OPAQUE: { return "OPAQUE"; }
        case RenderPassType::DIRECTIONAL_SHADOW: { return "DIRECTIONAL_SHADOW"; }
        default: { ENG_ERROR("Unhandled case"); return ""; }
    }
}

std::string to_string(const Flags<PipelineStage>& a)
{
	std::string str;
	const auto append = [&str](std::string_view cstr) {
			if(str.empty()) { str = cstr; }
			else { str += " | "; str += cstr;}
		};
	if(a == PipelineStage::NONE)			 { append("NONE"); }
	if(a.test(PipelineStage::TRANSFER_BIT))	 { append("TRANSFER_BIT"); }
	if(a.test(PipelineStage::VERTEX_BIT))	 { append("VERTEX_BIT"); }
	if(a.test(PipelineStage::FRAGMENT))		 { append("FRAGMENT"); }
	if(a.test(PipelineStage::EARLY_Z_BIT))	 { append("EARLY_Z_BIT"); }
	if(a.test(PipelineStage::LATE_Z_BIT))	 { append("LATE_Z_BIT"); }
	if(a.test(PipelineStage::COLOR_OUT_BIT)) { append("COLOR_OUT_BIT"); }
	if(a.test(PipelineStage::COMPUTE_BIT))	 { append("COMPUTE_BIT"); }
	if(a.test(PipelineStage::INDIRECT_BIT))	 { append("INDIRECT_BIT"); }
	if(a == PipelineStage::ALL)				 { append("ALL"); }

	return str;
}

std::string to_string(const Flags<PipelineAccess>& a)
{
	std::string str;
	const auto append = [&str](std::string_view cstr) {
			if(str.empty()) { str = cstr; }
			else { str += " | "; str += cstr;}
		};

	if(a == PipelineAccess::NONE)					{ append("NONE"); }
    if(a.test(PipelineAccess::SHADER_READ_BIT))		{ append("SHADER_READ_BIT"); }
    if(a.test(PipelineAccess::SHADER_WRITE_BIT))	{ append("SHADER_WRITE_BIT"); }
    if(a.test(PipelineAccess::SHADER_RW))			{ append("SHADER_RW"); }
    if(a.test(PipelineAccess::COLOR_READ_BIT))		{ append("COLOR_READ_BIT"); }
    if(a.test(PipelineAccess::COLOR_WRITE_BIT))		{ append("COLOR_WRITE_BIT"); }
    if(a.test(PipelineAccess::COLOR_RW_BIT))		{ append("COLOR_RW_BIT"); }
    if(a.test(PipelineAccess::DS_READ_BIT))			{ append("DS_READ_BIT"); }
    if(a.test(PipelineAccess::DS_WRITE_BIT))		{ append("DS_WRITE_BIT"); }
    if(a.test(PipelineAccess::DS_RW))				{ append("DS_RW"); }
    if(a.test(PipelineAccess::STORAGE_READ_BIT))	{ append("STORAGE_READ_BIT"); }
    if(a.test(PipelineAccess::STORAGE_WRITE_BIT))	{ append("STORAGE_WRITE_BIT"); }
    if(a.test(PipelineAccess::STORAGE_RW))			{ append("STORAGE_RW"); }
    if(a.test(PipelineAccess::INDIRECT_READ_BIT))	{ append("INDIRECT_READ_BIT"); }
    if(a.test(PipelineAccess::TRANSFER_READ_BIT))	{ append("TRANSFER_READ_BIT"); }
    if(a.test(PipelineAccess::TRANSFER_WRITE_BIT))	{ append("TRANSFER_WRITE_BIT"); }
    if(a.test(PipelineAccess::TRANSFER_RW))			{ append("TRANSFER_RW"); }

	return str;
}

// clang-format on

} // namespace gfx
} // namespace eng
