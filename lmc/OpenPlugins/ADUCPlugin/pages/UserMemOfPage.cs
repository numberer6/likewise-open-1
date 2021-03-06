/* Editor Settings: expandtabs and use 4 spaces for indentation
 * ex: set softtabstop=4 tabstop=8 expandtab shiftwidth=4: *
 * -*- mode: c, c-basic-offset: 4 -*- */

/*
 * Copyright Likewise Software    2004-2008
 * All rights reserved.
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the license, or (at
 * your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
 * General Public License for more details.  You should have received a copy
 * of the GNU Lesser General Public License along with this program.  If
 * not, see <http://www.gnu.org/licenses/>.
 *
 * LIKEWISE SOFTWARE MAKES THIS SOFTWARE AVAILABLE UNDER OTHER LICENSING
 * TERMS AS WELL.  IF YOU HAVE ENTERED INTO A SEPARATE LICENSE AGREEMENT
 * WITH LIKEWISE SOFTWARE, THEN YOU MAY ELECT TO USE THE SOFTWARE UNDER THE
 * TERMS OF THAT SOFTWARE LICENSE AGREEMENT INSTEAD OF THE TERMS OF THE GNU
 * LESSER GENERAL PUBLIC LICENSE, NOTWITHSTANDING THE ABOVE NOTICE.  IF YOU
 * HAVE QUESTIONS, OR WISH TO REQUEST A COPY OF THE ALTERNATE LICENSING
 * TERMS OFFERED BY LIKEWISE SOFTWARE, PLEASE CONTACT LIKEWISE SOFTWARE AT
 * license@likewisesoftware.com
 */

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Text;
using System.Windows.Forms;
using Likewise.LMC.Utilities;
using Likewise.LMC.ServerControl;
using Likewise.LMC.LDAP;
using Likewise.LMC.LDAP.Interop;
using System.DirectoryServices;


namespace Likewise.LMC.Plugins.ADUCPlugin
{
public partial class UserMemOfPage : MPPage, IDirectoryPropertiesPage
{
    #region Class Data
    string _servername = "";
    ADUCPlugin _plugin;
    IPlugInContainer _container;
    ADUCDirectoryNode _dirnode = null;

    bool memListchanged = false;

    List<string> MemofDnList;
    List<string> AddedGroups;
    List<string> RemovedGroups;

    private ListViewColumnSorter lvwColumnSorter;
    private string sPrimayGroup = string.Empty;
    private bool IsPrimaryGroupChanged = false;
    private string ChangedPrimaryGroup = "";

    #endregion

    #region Constructors
    public UserMemOfPage(IPlugInContainer container, ADUCPlugin plugin)
    {
        this.pageID = "UserMemofEditProperities";
        InitializeComponent();

        // Create an instance of a ListView column sorter and assign it
        // to the ListView control.
        lvwColumnSorter = new ListViewColumnSorter();
        this.MemoflistView.ListViewItemSorter = lvwColumnSorter;

        SetPageTitle("Member of");
        _plugin = plugin;
        _container = container;
        MemofDnList = new List<string>();
        AddedGroups = new List<string>();
        RemovedGroups = new List<string>();
    }

    #endregion

    #region Initialization Methods

    /// <summary>
    /// Gets all groups for the selected user AD Object is member of
    /// Add groups to the Member Of page listview
    /// </summary>
    /// <param name="ce"></param>
    /// <param name="servername"></param>
    /// <param name="name"></param>
    /// <param name="dirnode"></param>
    public void SetData(CredentialEntry ce, string servername, string name, ADUCDirectoryNode dirnode)
    {
        _servername = servername;
        _dirnode = dirnode;

        try
        {
            string[] groupDns = UserGroupUtils.GetGroupsforUser(dirnode);

            MemoflistView.Items.Clear();
            //show a list of group names in the member of page
            Logger.Log("user member of contains: ");
            if (groupDns != null && groupDns.Length > 0)
            {
                //populate the data in usermemberOf page using groupDns
                foreach (string groupDn in groupDns)
                {
                    Logger.Log("group: " + groupDn);
                    //CN=Domain Users,CN=Users,DC=qadom,DC=centeris,DC=com
                    // split the groupDns
                    string[] slvItem = UserGroupUtils.splitDn(groupDn);
                    string sLDAPPath = string.Format("LDAP://{0}/{1}", _dirnode.LdapContext.DomainName, groupDn);
                    DirectoryEntry entry = new DirectoryEntry(sLDAPPath, _dirnode.LdapContext.UserName, _dirnode.LdapContext.Password);
                    if (entry == null)
                    {
                        return;
                    }
                    ListViewItem lvItem = new ListViewItem(slvItem);
                    lvItem.ImageIndex = MemOfPages.GetIndexForADObject(entry);
                    MemoflistView.Items.Add(lvItem);
                    lvItem.Tag = groupDn;

                    if (!slvItem[0].Equals("Domain Users", StringComparison.InvariantCultureIgnoreCase))
                    {
                        MemofDnList.Add(groupDn);
                    }
                }
                //settings primary group to user
                sPrimayGroup = UserGroupUtils.GetPrimaryGroup(_dirnode);
                string[] Items = UserGroupUtils.splitDn(sPrimayGroup);
                if (!string.IsNullOrEmpty(Items[0]))
                {
                    DomainUserlabel.Text = Items[0];
                }

                if (MemoflistView.Items.Count > 0)
                {
                    MemoflistView.Items[0].Selected = true;
                }
            }
        }
        catch (Exception ex)
        {
            Logger.LogException("UserMemberOfPage.SetData", ex);
        }
    }
    #endregion

    #region Methods
    private void UpdateApplyButton()
    {
        if (!memListchanged)
        {
            ParentContainer.DataChanged = false;
            ParentContainer.btnApply.Enabled = false;
        }
        else
        {
            ParentContainer.DataChanged = true;
            ParentContainer.btnApply.Enabled = true;
        }
    }

    /// <summary>
    /// when adding a user to a new group, we need modify the group's "member" attribute to include this user,
    /// we cannot modify the user's "memberof" attribute
    /// </summary>
    /// <returns></returns>
    public bool OnApply()
    {
        bool retVal = true;
        if (IsPrimaryGroupChanged)
        {
            List<LDAPMod> attrlist = new List<LDAPMod>();
            //the following portion of code uses openldap "ldap_Modify_s"
            string basedn = _dirnode.DistinguishedName;
            string[] objectClass_values = null;

            //first obtain the current primaryGroupID value
            DirectoryEntry de = new DirectoryEntry(string.Format("LDAP://{0}/{1}", _dirnode.LdapContext.DomainName, ChangedPrimaryGroup));
            if (de.Properties["primaryGroupToken"].Value != null)
            {
                int iPrimaryGroupToken = Convert.ToInt32(de.Properties["primaryGroupToken"].Value.ToString());

                objectClass_values = new string[] { iPrimaryGroupToken.ToString(), null };
                LDAPMod attr_info =
                new LDAPMod((int)LDAPMod.mod_ops.LDAP_MOD_REPLACE, "primaryGroupID",
                objectClass_values);

                LDAPMod[] attrinfo = new LDAPMod[] { attr_info };
                int ret = _dirnode.LdapContext.ModifySynchronous(basedn, attrinfo);
                Logger.Log("Modify primaryGroupID returns " + ret);
                if (ret == 0)
                {
                    string[] Items = UserGroupUtils.splitDn(ChangedPrimaryGroup);
                    if (!string.IsNullOrEmpty(Items[0]))
                    {
                        DomainUserlabel.Text = Items[0];
                    }
                }
                else
                    retVal = false;
            }
        }

        retVal = MemOfPages.OnApply_helper(MemofDnList, AddedGroups, RemovedGroups, _dirnode, this);

        return retVal;
    }

    /// <summary>
    /// Method to find the duplicate entry from the list
    /// </summary>
    /// <param name="item"></param>
    /// <returns></returns>
    private bool IsItemExists(string DN)
    {
        bool bIsFound=false;
        foreach (ListViewItem lvItem in MemoflistView.Items)
        {
            string Dn = lvItem.Tag as string;
            if (Dn.Trim().Equals(DN))
            {
                bIsFound = true;
            }
        }
        return bIsFound;
    }

    #endregion

    #region Events

    /// <summary>
    /// Populate the DsPicker model dialog
    /// Gets the selected group and add it to the list, removed from the RemovedGroups list if it is exists
    /// </summary>
    /// <param name="sender"></param>
    /// <param name="e"></param>
    private void Addbutton_Click(object sender, EventArgs e)
    {
        // show picker
        string sLdapPath = string.Format("LDAP://{0}/{1}", _dirnode.LdapContext.DomainName, _dirnode.DistinguishedName);
        string sProtocol;
        string sServer;
        string sCNs;
        string sDCs;

        System.DirectoryServices.SDSUtils.CrackPath(sLdapPath, out sProtocol, out sServer, out sCNs, out sDCs);
        System.DirectoryServices.Misc.DsPicker f = new System.DirectoryServices.Misc.DsPicker();
        f.SetData(System.DirectoryServices.Misc.DsPicker.DialogType.SELECT_GROUPS, sProtocol, sServer, sDCs, true);

        if (f.waitForm != null && f.waitForm.bIsInterrupted)
        {
            return;
        }

        if (f.ShowDialog(this) == DialogResult.OK)
        {
            if (f.ADobjectsArray != null && f.ADobjectsArray.Length != 0)
            {
                foreach (System.DirectoryServices.Misc.ADObject ado in f.ADobjectsArray)
                {
                    bool bIsObjectExists = false;
                    string sDN = ado.de.Properties["distinguishedName"].Value as string;
                    string[] slvItem = UserGroupUtils.splitDn(sDN);
                    ListViewItem lvItem = new ListViewItem(slvItem);
                    if (IsItemExists(sDN))
                    {
                        string sMsg =
                        "The object " +
                        slvItem[0] +
                        " is already in the list \nand cannot be added a second time";
                        container.ShowError(sMsg);
                        bIsObjectExists = true;
                    }
                    if (!bIsObjectExists)
                    {
                        string sLDAPPath = string.Format("LDAP://{0}/{1}", _dirnode.LdapContext.DomainName, sDN);
                        DirectoryEntry entry = new DirectoryEntry(sLDAPPath, _dirnode.LdapContext.UserName, _dirnode.LdapContext.Password);
                        if (entry == null)
                        {
                            return;
                        }

                        lvItem.ImageIndex = MemOfPages.GetIndexForADObject(entry);
                        MemoflistView.Items.Add(lvItem);
                        lvItem.Tag = sDN;

                        if (!slvItem[0].Equals("Domain Users", StringComparison.InvariantCultureIgnoreCase))
                        {
                            MemofDnList.Add(sDN);
                            AddedGroups.Add(sDN);
                            foreach (string str in RemovedGroups)
                            {
                                if (str.Equals(sDN, StringComparison.InvariantCultureIgnoreCase))
                                {
                                    RemovedGroups.Remove(sDN);
                                    break;
                                }
                            }
                        }
                        memListchanged = true;
                    }
                }
            }
        }
        else
        {
            memListchanged = false;
        }

        UpdateApplyButton();

    }

    /// <summary>
    /// Removes the selected user from the Member of listview
    /// </summary>
    /// <param name="sender"></param>
    /// <param name="e"></param>
    private void RemoveButton_Click(object sender, EventArgs e)
    {
        if (MemoflistView.SelectedItems.Count > 0)
        {
            DialogResult dlg =
            MessageBox.Show(this, "Do you want to remove " + _dirnode.Text + " from the selected group(s)?",
            "Remove user from group", MessageBoxButtons.YesNo,
            MessageBoxIcon.Exclamation, MessageBoxDefaultButton.Button2);
            if (dlg == DialogResult.Yes)
            {
                foreach (ListViewItem item in MemoflistView.SelectedItems)
                {
                    bool bIsPrimaryGroup = false;
                    string removeDn = item.Tag as string;
                    string[] slvItem = UserGroupUtils.splitDn(removeDn);
                    if (!string.IsNullOrEmpty(slvItem[0]) && DomainUserlabel.Text.Trim().Equals(slvItem[0]))
                    {
                        bIsPrimaryGroup = true;
                        string sMsg =
                        "The primary group cannot be removed. Set another group " +
                        "as \nprimary if you want to remove this one";
                        container.ShowError(this.ParentForm, sMsg);
                    }
                    if (!bIsPrimaryGroup)
                    {

                        ListViewItem[] lvItemArr = new ListViewItem[MemoflistView.Items.Count - 1];
                        List<ListViewItem> lvItemList = new List<ListViewItem>();
                        int i = 0;

                        foreach (ListViewItem lvitem in MemoflistView.Items)
                        {
                            if (!lvitem.Equals(item))
                            {
                                lvItemList.Add(lvitem);
                            }
                        }
                        foreach (ListViewItem lvitem in lvItemList)
                        {
                            lvItemArr[i++] = lvitem;
                        }

                        MemoflistView.Items.Clear();
                        MemoflistView.Items.AddRange(lvItemArr);

                        RemovedGroups.Add(removeDn);
                        //remove the dn from added group list
                        foreach (string str in AddedGroups)
                        {
                            if (str.Equals(removeDn, StringComparison.InvariantCultureIgnoreCase))

{

}


                            AddedGroups.Remove(removeDn);
                            RemovedGroups.Remove(removeDn);
                            break;
                        }
                    }
                }
                RemoveButton.Enabled = false;

                memListchanged = true;
            }
        }
        else
        {
            memListchanged = false;
        }

        UpdateApplyButton();
    }

    private void SetPrimaryGroupbutton_Click(object sender, EventArgs e)
    {
        if (MemoflistView.SelectedItems.Count == 0)
        {
            return;
        }

        string DN = MemoflistView.SelectedItems[0].Tag.ToString();
        if (DN == null)
        {
            return;
        }
        if (!sPrimayGroup.Equals(DN, StringComparison.InvariantCultureIgnoreCase))
        {
            IsPrimaryGroupChanged = true;
            ChangedPrimaryGroup = DN;
            DomainUserlabel.Text = MemoflistView.SelectedItems[0].Text;

            ParentContainer.btnApply.Enabled = ParentContainer.DataChanged = true;
        }
    }

    private void MemoflistView_SelectedIndexChanged(object sender, EventArgs e)
    {
        RemoveButton.Enabled = MemoflistView.SelectedItems.Count != 0;

        if (MemoflistView.SelectedItems.Count == 0)
        {
            return;
        }

        if (MemoflistView.SelectedItems.Count > 1)
        {
            SetPrimaryGroupbutton.Enabled = false;
        }
        else
        {
            ListViewItem lvItem = MemoflistView.SelectedItems[0] as ListViewItem;
            string DN = lvItem.Tag as string;
            if (!String.IsNullOrEmpty(DN))
            {
                if (DN.Trim().Equals(sPrimayGroup.Trim(), StringComparison.InvariantCultureIgnoreCase))
                {
                    SetPrimaryGroupbutton.Enabled = false;
                    return;
                }
                else
                {
                    string sLDAPPath = string.Format("LDAP://{0}/{1}", _dirnode.LdapContext.DomainName, DN);
                    DirectoryEntry entry = new DirectoryEntry(sLDAPPath, _dirnode.LdapContext.UserName, _dirnode.LdapContext.Password);
                    if (entry == null)
                    {
                        return;
                    }

                    object[] asProp = entry.Properties["objectClass"].Value as object[];
                    // poke these in a list for easier reference
                    List<string> liClasses = new List<string>();
                    foreach (string s in asProp)
                    {
                        liClasses.Add(s);
                    }

                    if (liClasses.Contains("group"))
                    {
                        string primaryGpToken = string.Empty;
                        if (entry.Properties["groupType"].Value != null)
                        {
                            primaryGpToken = entry.Properties["groupType"].Value.ToString();
                        }
                        if (primaryGpToken != null)
                        {
                            if ((primaryGpToken.Trim().Equals("-2147483646")) ||
                            (primaryGpToken.Trim().Equals("-2147483640")))
                            {
                                SetPrimaryGroupbutton.Enabled = true;
                            }
                            else
                            {
                                SetPrimaryGroupbutton.Enabled = false;
                            }
                        }
                    }
                }
            }
        }

    }

    private void MemoflistView_ColumnClick(object sender, ColumnClickEventArgs e)
    {
        // Determine if clicked column is already the column that is being sorted.
        if (e.Column == lvwColumnSorter.SortColumn)
        {
            // Reverse the current sort direction for this column.
            if (lvwColumnSorter.Order == SortOrder.Ascending)
            {
                lvwColumnSorter.Order = SortOrder.Descending;
            }
            else
            {
                lvwColumnSorter.Order = SortOrder.Ascending;
            }
        }
        else
        {
            // Set the column number that is to be sorted; default to ascending.
            lvwColumnSorter.SortColumn = e.Column;
            lvwColumnSorter.Order = SortOrder.Ascending;
        }

        // Perform the sort with these new sort options.
        this.MemoflistView.Sort();
    }

    private void MemoflistView_MouseDoubleClick(object sender, MouseEventArgs e)
    {
        if (MemoflistView.SelectedItems.Count > 0)
        {
            ListViewItem selectedItem = MemoflistView.SelectedItems[0];
            if (selectedItem.Tag != null)
            {
                string distinguishedName = selectedItem.Tag.ToString();
                if (distinguishedName.Equals(_dirnode.DistinguishedName, StringComparison.InvariantCultureIgnoreCase))
                {
                    return;
                }
                ADUCPlugin plugin = _dirnode.Plugin as ADUCPlugin;
                if (plugin.Propertywindowhandles.ContainsKey(distinguishedName))
                {
                    Form f = plugin.Propertywindowhandles[distinguishedName] as Form;
                    f.BringToFront();
                    return;
                }

                string sLDAPPath = string.Format("LDAP://{0}/{1}", _dirnode.LdapContext.DomainName, distinguishedName);
                DirectoryEntry entry = new DirectoryEntry(sLDAPPath);
                if (entry == null)
                {
                    return;
                }

                object[] asProp = entry.Properties["objectClass"].Value as object[];

                // poke these in a list for easier reference
                List<string> liClasses = new List<string>();
                foreach (string s in asProp)
                {
                    liClasses.Add(s);
                }

                if (liClasses.Contains("computer"))
                {
                    ADUCDirectoryNode dirnode = new ADUCDirectoryNode(distinguishedName,
                    _dirnode.LdapContext, "computer",
                    Properties.Resources.computer, _dirnode.t, _dirnode.Plugin, _dirnode.IsDisabled);
                    ADComputerPropertiesDlg f = new ADComputerPropertiesDlg(base.container, _dirnode.PluginPage as ADUCPage, _plugin);
                    if (_dirnode.LdapContext.SchemaCache != null)
                    {
                        f.SetData(_plugin.HostInfo.creds, _plugin.HostInfo.hostName, dirnode.Text.Substring(3), dirnode, _dirnode.LdapContext.SchemaCache);
                        f.Show();
                    }
                }
                else if (liClasses.Contains("user"))
                {
                    ADUCDirectoryNode dirnode = new ADUCDirectoryNode(distinguishedName,
                    _dirnode.LdapContext, "user",
                    Properties.Resources.User, _dirnode.t, _dirnode.Plugin, _dirnode.IsDisabled);

                    List<object> dirnodes = new List<object>();
                    dirnodes.Add(dirnode);
                    ADUserPropertiesDlg f = new ADUserPropertiesDlg(base.container, _dirnode.PluginPage as ADUCPage, _plugin, dirnodes);
                    if (_dirnode.LdapContext.SchemaCache != null)
                    {
                        f.SetData(_plugin.HostInfo.creds, _plugin.HostInfo.hostName, dirnode.Text.Substring(3), dirnode, _dirnode.LdapContext.SchemaCache);
                        f.Show();
                    }
                }
                else if (liClasses.Contains("group"))
                {
                    ADUCDirectoryNode dirnode = new ADUCDirectoryNode(distinguishedName,
                    _dirnode.LdapContext, "group",
                    Properties.Resources.Group_48, _dirnode.t, _dirnode.Plugin, _dirnode.IsDisabled);
                    ADGroupPropertiesDlg f = new ADGroupPropertiesDlg(base.container, _dirnode.PluginPage as ADUCPage, _plugin, dirnode);
                    if (_dirnode.LdapContext.SchemaCache != null)
                    {
                        f.SetData(_plugin.HostInfo.creds, _plugin.HostInfo.hostName, dirnode.Text.Substring(3), dirnode, _dirnode.LdapContext.SchemaCache);
                        f.Show();
                    }
                }
                else if (liClasses.Contains("foreignSecurityPrincipal"))
                {
                    ADUCDirectoryNode dirnode = new ADUCDirectoryNode(distinguishedName,
                    _dirnode.LdapContext, "foreignSecurityPrincipal",
                    Properties.Resources.Group_48, _dirnode.t, _dirnode.Plugin, _dirnode.IsDisabled);
                    ADGroupPropertiesDlg f = new ADGroupPropertiesDlg(base.container, _dirnode.PluginPage as ADUCPage, _plugin, dirnode);
                    if (_dirnode.LdapContext.SchemaCache != null)
                    {
                        f.SetData(_plugin.HostInfo.creds, _plugin.HostInfo.hostName, dirnode.Text.Substring(3), dirnode, _dirnode.LdapContext.SchemaCache);
                        f.Show();
                    }
                }
            }
        }
    }
    #endregion
}
}
