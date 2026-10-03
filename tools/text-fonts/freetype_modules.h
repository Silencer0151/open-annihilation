// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The FreeType modules the engine's text fonts need, in FreeType's own order
// (include/freetype/config/ftmodule.h): the TrueType driver (DejaVu Sans,
// Noto Emoji), the CFF driver (Noto Sans CJK) with the PostScript helpers
// it uses, the SFNT tables both read, the auto-hinter, and the anti-aliasing
// and monochrome rasterizers. tools/text-fonts/freetype_build_options.cmake
// names this file to FreeType's build.

FT_USE_MODULE(FT_Module_Class, autofit_module_class)
FT_USE_MODULE(FT_Driver_ClassRec, tt_driver_class)
FT_USE_MODULE(FT_Driver_ClassRec, cff_driver_class)
FT_USE_MODULE(FT_Module_Class, psaux_module_class)
FT_USE_MODULE(FT_Module_Class, psnames_module_class)
FT_USE_MODULE(FT_Module_Class, pshinter_module_class)
FT_USE_MODULE(FT_Module_Class, sfnt_module_class)
FT_USE_MODULE(FT_Renderer_Class, ft_smooth_renderer_class)
FT_USE_MODULE(FT_Renderer_Class, ft_raster1_renderer_class)
