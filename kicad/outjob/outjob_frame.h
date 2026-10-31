/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef OUTJOB_FRAME_H
#define OUTJOB_FRAME_H

#include <memory>
#include <vector>

#include <wx/frame.h>
#include <wx/string.h>

class wxBoxSizer;
class wxChoice;
class wxGrid;
class wxGridEvent;
class wxMenu;
class wxScrolledWindow;
class wxStaticText;
class JOBSET;
struct JOBSET_DESTINATION;
class KICAD_MANAGER_FRAME;
class OUTJOB_CONTAINER_PANEL;


/**
 * Altium-style Output Job editor over a KiCad job set (.kicad_jobset).
 *
 * Left: the outputs (jobs) grouped in Altium's categories, each with an "Enabled" box for the
 * selected output container.  Right: the output containers (job set destinations) with
 * "Generate Content".  The variant choice applies to every variant-aware output.
 *
 * One window per file; it lives in the project manager process and is reached from the
 * editors through MAIL_OPEN_OUTPUT_JOB (see include/outputs/output_job_link.h).
 */
class OUTJOB_FRAME : public wxFrame
{
public:
    /// Open (or raise) the editor for aPath.  A missing file is created with a default
    /// "Folder Structure" container.
    static OUTJOB_FRAME* Open( KICAD_MANAGER_FRAME* aManager, const wxString& aPath );

    /// Hook the project manager so editors can open Output Jobs (call once from its ctor).
    static void InstallManagerHooks( KICAD_MANAGER_FRAME* aManager );

    /// Menu id of the manager's "Open Output Job..." item.
    static int ManagerMenuId();

    ~OUTJOB_FRAME() override;

    wxString GetFilePath() const;

    /// Ask to save unsaved changes.  False when the user cancelled.
    bool ConfirmClose( bool aCanVeto );

    // Used by the container panels
    void SelectContainer( const wxString& aDestinationId );
    void EditContainer( const wxString& aDestinationId );
    void RemoveContainer( const wxString& aDestinationId );
    void GenerateContainer( const wxString& aDestinationId );
    void OpenContainerOutput( const wxString& aDestinationId );
    void ShowContainerLog( const wxString& aDestinationId );
    JOBSET_DESTINATION* FindDestination( const wxString& aDestinationId );
    JOBSET* GetJobSet() { return m_jobSet.get(); }

private:
    OUTJOB_FRAME( KICAD_MANAGER_FRAME* aManager, std::unique_ptr<JOBSET> aJobSet );

    struct ROW
    {
        enum KIND { CATEGORY, JOB, ADD } kind;
        int    category;
        size_t job;      ///< index into the job set's jobs (JOB rows)
    };

    void buildMenuBar();
    void rebuildGrid();
    void rebuildContainers();
    void updateTitle();
    void updateVariantChoice();
    void setDirty();

    int  selectedJob() const;     ///< job index of the grid cursor row, or -1
    void appendAddMenu( wxMenu* aMenu, int aCategory );
    void addJob( const wxString& aType );
    bool configureJob( size_t aJobIdx );
    void deleteJob( size_t aJobIdx );
    void moveJob( size_t aJobIdx, bool aUp );
    void toggleEnabled( size_t aJobIdx );
    void addContainer( int aType );
    void ensureEditorsOpen();
    bool save();
    void runDestinations( const std::vector<wxString>& aIds );

    void onGridLeftClick( wxGridEvent& aEvent );
    void onGridDClick( wxGridEvent& aEvent );
    void onGridRightClick( wxGridEvent& aEvent );
    void onGridCellChanged( wxGridEvent& aEvent );
    void onMenu( wxCommandEvent& aEvent );
    void onVariantChoice( wxCommandEvent& aEvent );
    void onClose( wxCloseEvent& aEvent );
    void onManagerClose( wxCloseEvent& aEvent );

private:
    KICAD_MANAGER_FRAME*                 m_manager;
    std::unique_ptr<JOBSET>              m_jobSet;
    std::vector<ROW>                     m_rows;
    wxString                             m_selectedContainer;

    wxGrid*                              m_grid;
    wxScrolledWindow*                    m_containerList;
    wxBoxSizer*                          m_containerSizer;
    std::vector<OUTJOB_CONTAINER_PANEL*> m_containerPanels;
    wxChoice*                            m_variantChoice;
    wxStaticText*                        m_pathLabel;
    wxArrayString                        m_variantValues;   ///< parallel to m_variantChoice
};

#endif // OUTJOB_FRAME_H
