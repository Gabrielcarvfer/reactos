/*
 * PROJECT:     shell32
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Start menu pinned list (StartMenuPin, Windows 7)
 */

#pragma once

/*
 * IStartMenuPinnedList: what installers call to unpin a program from the Start
 * menu and the taskbar when it goes away. ReactOS's Start menu keeps no list
 * of pinned programs, so there is nothing to remove. (Foxit's and CDBurnerXP's
 * setups create this object, and gave up when it did not exist.)
 */
class CStartMenuPin :
    public CComCoClass<CStartMenuPin, &CLSID_StartMenuPin>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IStartMenuPinnedList
{
public:
    // IStartMenuPinnedList
    STDMETHODIMP RemoveFromList(IShellItem *pitem) override
    {
        return pitem ? S_OK : E_INVALIDARG;
    }

    DECLARE_REGISTRY_RESOURCEID(IDR_STARTMENUPIN)
    DECLARE_NOT_AGGREGATABLE(CStartMenuPin)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CStartMenuPin)
        COM_INTERFACE_ENTRY_IID(IID_IStartMenuPinnedList, IStartMenuPinnedList)
    END_COM_MAP()
};
