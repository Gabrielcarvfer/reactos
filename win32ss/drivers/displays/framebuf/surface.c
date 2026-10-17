/*
 * ReactOS Generic Framebuffer display driver
 *
 * Copyright (C) 2004 Filip Navara
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "framebuf.h"

/*
 * IntCopyToScreen
 *
 * Copies a rectangle of the shadow surface to the screen. Only writes to
 * video memory: reads from it are uncached and very slow.
 */

static VOID
IntCopyToScreen(
   IN PPDEV ppdev,
   IN const RECTL *prcl)
{
   RECTL rcl;
   ULONG BytesPerPixel = (ppdev->BitsPerPixel + 7) / 8;
   ULONG Offset, Width;
   PBYTE Src, Dst;
   LONG y;

   if (!ppdev->ShadowPtr || !ppdev->ScreenEnabled)
      return;

   rcl.left = 0;
   rcl.top = 0;
   rcl.right = ppdev->ScreenWidth;
   rcl.bottom = ppdev->ScreenHeight;
   if (prcl)
   {
      rcl.left = max(rcl.left, prcl->left);
      rcl.top = max(rcl.top, prcl->top);
      rcl.right = min(rcl.right, prcl->right);
      rcl.bottom = min(rcl.bottom, prcl->bottom);
   }
   if (rcl.left >= rcl.right || rcl.top >= rcl.bottom)
      return;

   Offset = rcl.top * ppdev->ScreenDelta + rcl.left * BytesPerPixel;
   Width = (rcl.right - rcl.left) * BytesPerPixel;
   Src = (PBYTE)ppdev->ShadowPtr + Offset;
   Dst = (PBYTE)ppdev->ScreenPtr + Offset;

   if (Width == ppdev->ScreenDelta)
   {
      /* Whole lines: one copy */
      RtlCopyMemory(Dst, Src, Width * (rcl.bottom - rcl.top));
      return;
   }

   for (y = rcl.top; y < rcl.bottom; y++)
   {
      RtlCopyMemory(Dst, Src, Width);
      Src += ppdev->ScreenDelta;
      Dst += ppdev->ScreenDelta;
   }
}

/*
 * DrvSynchronizeSurface
 *
 * GDI tells us it drew into the rectangle *prcl (NULL: anywhere) of the
 * shadow surface: show it.
 *
 * Status
 *    @implemented
 */

VOID APIENTRY
DrvSynchronizeSurface(
   IN SURFOBJ *pso,
   IN RECTL *prcl,
   IN FLONG fl)
{
   PPDEV ppdev = (PPDEV)pso->dhpdev;

   /* Without a flush/timer flag, GDI is about to draw: nothing to wait for */
   if (!(fl & (DSS_FLUSH_EVENT | DSS_TIMER_EVENT)))
      return;

   IntCopyToScreen(ppdev, prcl);
}

/*
 * DrvEnableSurface
 *
 * Create engine bitmap around frame buffer and set the video mode requested
 * when PDEV was initialized.
 *
 * Status
 *    @implemented
 */

HSURF APIENTRY
DrvEnableSurface(
   IN DHPDEV dhpdev)
{
   PPDEV ppdev = (PPDEV)dhpdev;
   HSURF hSurface;
   ULONG BitmapType;
   SIZEL ScreenSize;
   VIDEO_MEMORY VideoMemory;
   VIDEO_MEMORY_INFORMATION VideoMemoryInfo;
   ULONG ulTemp;

   /*
    * Set video mode of our adapter.
    */

   if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_SET_CURRENT_MODE,
                          &(ppdev->ModeIndex), sizeof(ULONG), NULL, 0,
                          &ulTemp))
   {
      return NULL;
   }

   /*
    * Map the framebuffer into our memory.
    */

   VideoMemory.RequestedVirtualAddress = NULL;
   if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_MAP_VIDEO_MEMORY,
                          &VideoMemory, sizeof(VIDEO_MEMORY),
                          &VideoMemoryInfo, sizeof(VIDEO_MEMORY_INFORMATION),
                          &ulTemp))
   {
      return NULL;
   }

   ppdev->ScreenPtr = VideoMemoryInfo.FrameBufferBase;
   ppdev->ScreenEnabled = TRUE;

   switch (ppdev->BitsPerPixel)
   {
      case 8:
         IntSetPalette(dhpdev, ppdev->PaletteEntries, 0, 256);
         BitmapType = BMF_8BPP;
         break;

      case 16:
         BitmapType = BMF_16BPP;
         break;

      case 24:
         BitmapType = BMF_24BPP;
         break;

      case 32:
         BitmapType = BMF_32BPP;
         break;

      default:
         return NULL;
   }

   ppdev->iDitherFormat = BitmapType;

   ScreenSize.cx = ppdev->ScreenWidth;
   ScreenSize.cy = ppdev->ScreenHeight;

   /*
    * Let GDI draw into a copy of the screen in system memory: drawing that
    * reads the destination (moving a window, alpha blending, the software
    * mouse pointer) would otherwise read video memory, which is uncached.
    */

   ppdev->ShadowPtr = NULL;
   if (ppdev->ScreenDelta > 0)
   {
      ppdev->ShadowPtr = EngAllocMem(FL_ZERO_MEMORY,
                                     ppdev->ScreenDelta * ppdev->ScreenHeight,
                                     ALLOC_TAG);
   }

   hSurface = (HSURF)EngCreateBitmap(ScreenSize, ppdev->ScreenDelta, BitmapType,
                                     (ppdev->ScreenDelta > 0) ? BMF_TOPDOWN : 0,
                                     ppdev->ShadowPtr ? ppdev->ShadowPtr : ppdev->ScreenPtr);
   if (hSurface == NULL)
   {
      goto Failure;
   }

   /*
    * Associate the surface with our device. With a shadow, GDI calls
    * DrvSynchronizeSurface with what it changed (GCAPS2_SYNCFLUSH).
    */

   if (!EngAssociateSurface(hSurface, ppdev->hDevEng,
                            ppdev->ShadowPtr ? HOOK_SYNCHRONIZE : 0))
   {
      EngDeleteSurface(hSurface);
      goto Failure;
   }

   /* Start from the same (black) picture as the shadow */
   IntCopyToScreen(ppdev, NULL);

   ppdev->hSurfEng = hSurface;

   return hSurface;

Failure:
   if (ppdev->ShadowPtr)
   {
      EngFreeMem(ppdev->ShadowPtr);
      ppdev->ShadowPtr = NULL;
   }
   ppdev->ScreenEnabled = FALSE;
   VideoMemory.RequestedVirtualAddress = ppdev->ScreenPtr;
   EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_UNMAP_VIDEO_MEMORY,
                      &VideoMemory, sizeof(VIDEO_MEMORY), NULL, 0, &ulTemp);
   return NULL;
}

/*
 * DrvDisableSurface
 *
 * Used by GDI to notify a driver that the surface created by DrvEnableSurface
 * for the current device is no longer needed.
 *
 * Status
 *    @implemented
 */

VOID APIENTRY
DrvDisableSurface(
   IN DHPDEV dhpdev)
{
   DWORD ulTemp;
   VIDEO_MEMORY VideoMemory;
   PPDEV ppdev = (PPDEV)dhpdev;

   ppdev->ScreenEnabled = FALSE;
   EngDeleteSurface(ppdev->hSurfEng);
   ppdev->hSurfEng = NULL;

   if (ppdev->ShadowPtr)
   {
      EngFreeMem(ppdev->ShadowPtr);
      ppdev->ShadowPtr = NULL;
   }

#ifdef EXPERIMENTAL_MOUSE_CURSOR_SUPPORT
   /* Clear all mouse pointer surfaces. */
   DrvSetPointerShape(NULL, NULL, NULL, NULL, 0, 0, 0, 0, NULL, 0);
#endif

   /*
    * Unmap the framebuffer.
    */

   VideoMemory.RequestedVirtualAddress = ((PPDEV)dhpdev)->ScreenPtr;
   EngDeviceIoControl(((PPDEV)dhpdev)->hDriver, IOCTL_VIDEO_UNMAP_VIDEO_MEMORY,
                      &VideoMemory, sizeof(VIDEO_MEMORY), NULL, 0, &ulTemp);
}

/*
 * DrvAssertMode
 *
 * Sets the mode of the specified physical device to either the mode specified
 * when the PDEV was initialized or to the default mode of the hardware.
 *
 * Status
 *    @implemented
 */

BOOL APIENTRY
DrvAssertMode(
   IN DHPDEV dhpdev,
   IN BOOL bEnable)
{
   PPDEV ppdev = (PPDEV)dhpdev;
   ULONG ulTemp;

   if (bEnable)
   {
      /*
       * Reinitialize the device to a clean state.
       */
      if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_SET_CURRENT_MODE,
                             &(ppdev->ModeIndex), sizeof(ULONG), NULL, 0,
                             &ulTemp))
      {
          /* We failed, bail out */
          return FALSE;
      }
      if (ppdev->BitsPerPixel == 8)
      {
	     IntSetPalette(dhpdev, ppdev->PaletteEntries, 0, 256);
      }

      /* The mode set cleared the screen: show the shadow again */
      ppdev->ScreenEnabled = TRUE;
      IntCopyToScreen(ppdev, NULL);

      return TRUE;
   }
   else
   {
      /*
       * Call the miniport driver to reset the device to a known state.
       */
      ppdev->ScreenEnabled = FALSE;
      return !EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_RESET_DEVICE,
                                 NULL, 0, NULL, 0, &ulTemp);
   }
}
