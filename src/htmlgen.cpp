/******************************************************************************
 *
 * Copyright (C) 1997-2021 by Dimitri van Heesch.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation under the terms of the GNU General Public License is hereby
 * granted. No representations are made about the suitability of this software
 * for any purpose. It is provided "as is" without express or implied warranty.
 * See the GNU General Public License for more details.
 *
 * Documents produced by Doxygen are derivative works derived from the
 * input used in their production; they are not affected by this license.
 *
 */

#include <stdlib.h>
#include <assert.h>

#include <mutex>
#include <sstream>

#include "message.h"
#include "htmlgen.h"
#include "config.h"
#include "util.h"
#include "doxygen.h"
#include "diagram.h"
#include "version.h"
#include "dot.h"
#include "dotcallgraph.h"
#include "dotclassgraph.h"
#include "dotdirdeps.h"
#include "dotgfxhierarchytable.h"
#include "dotgroupcollaboration.h"
#include "dotincldepgraph.h"
#include "language.h"
#include "htmlhelp.h"
#include "docparser.h"
#include "htmldocvisitor.h"
#include "searchindex.h"
#include "pagedef.h"
#include "debug.h"
#include "dirdef.h"
#include "vhdldocgen.h"
#include "layout.h"
#include "image.h"
#include "ftvhelp.h"
#include "bufstr.h"
#include "resourcemgr.h"
#include "tooltip.h"
#include "growbuf.h"
#include "fileinfo.h"
#include "dir.h"
#include "utf8.h"
#include "textstream.h"

//#define DBG_HTML(x) x;
#define DBG_HTML(x)

static QCString g_header;
static QCString g_footer;
static QCString g_mathjax_code;
static QCString g_latex_macro;
static const char *hex="0123456789ABCDEF";

// note: this is only active if DISABLE_INDEX=YES, if DISABLE_INDEX is disabled, this
// part will be rendered inside menu.js
static void writeClientSearchBox(TextStream &t,const QCString &relPath)
{
  t << "        <div id=\"MSearchBox\" class=\"MSearchBoxInactive\">\n";
  t << "        <span class=\"left\">\n";
  t << "          <img id=\"MSearchSelect\" src=\"" << relPath << "search/mag_sel.svg\"\n";
  t << "               onmouseover=\"return searchBox.OnSearchSelectShow()\"\n";
  t << "               onmouseout=\"return searchBox.OnSearchSelectHide()\"\n";
  t << "               alt=\"\"/>\n";
  t << "          <input type=\"text\" id=\"MSearchField\" value=\""
    << theTranslator->trSearch() << "\" accesskey=\"S\"\n";
  t << "               onfocus=\"searchBox.OnSearchFieldFocus(true)\" \n";
  t << "               onblur=\"searchBox.OnSearchFieldFocus(false)\" \n";
  t << "               onkeyup=\"searchBox.OnSearchFieldChange(event)\"/>\n";
  t << "          </span><span class=\"right\">\n";
  t << "            <a id=\"MSearchClose\" href=\"javascript:searchBox.CloseResultsWindow()\">"
    << "<img id=\"MSearchCloseImg\" border=\"0\" src=\"" << relPath << "search/close.svg\" alt=\"\"/></a>\n";
  t << "          </span>\n";
  t << "        </div>\n";
}

// note: this is only active if DISABLE_INDEX=YES. if DISABLE_INDEX is disabled, this
// part will be rendered inside menu.js
static void writeServerSearchBox(TextStream &t,const QCString &relPath,bool highlightSearch)
{
  bool externalSearch = Config_getBool(EXTERNAL_SEARCH);
  t << "        <div id=\"MSearchBox\" class=\"MSearchBoxInactive\">\n";
  t << "          <div class=\"left\">\n";
  t << "            <form id=\"FSearchBox\" action=\"" << relPath;
  if (externalSearch)
  {
    t << "search" << Doxygen::htmlFileExtension;
  }
  else
  {
    t << "search.php";
  }
  t << "\" method=\"get\">\n";
  t << "              <img id=\"MSearchSelect\" src=\"" << relPath << "search/mag.svg\" alt=\"\"/>\n";
  if (!highlightSearch)
  {
    t << "              <input type=\"text\" id=\"MSearchField\" name=\"query\" value=\""
      << theTranslator->trSearch() << "\" size=\"20\" accesskey=\"S\" \n";
    t << "                     onfocus=\"searchBox.OnSearchFieldFocus(true)\" \n";
    t << "                     onblur=\"searchBox.OnSearchFieldFocus(false)\"/>\n";
    t << "            </form>\n";
    t << "          </div><div class=\"right\"></div>\n";
    t << "        </div>\n";
  }
}

//------------------------------------------------------------------------
/// Convert a set of LaTeX  commands `\(re)newcommand`  to a form readable by MathJax
/// LaTeX syntax:
/// ```
///       \newcommand{\cmd}{replacement}
///         or
///       \renewcommand{\cmd}{replacement}
/// ```
/// MathJax syntax:
/// ```
///        cmd: "{replacement}"
/// ```
///
/// LaTeX syntax:
/// ```
///       \newcommand{\cmd}[nr]{replacement}
///         or
///       \renewcommand{\cmd}[nr]{replacement}
/// ```
/// MathJax syntax:
/// ```
///        cmd: ["{replacement}",nr]
/// ```
static QCString getConvertLatexMacro()
{
  QCString macrofile = Config_getString(FORMULA_MACROFILE);
  if (macrofile.isEmpty()) return "";
  QCString s = fileToString(macrofile);
  macrofile = FileInfo(macrofile.str()).absFilePath();
  size_t size = s.length();
  GrowBuf out(size);
  const char *data = s.data();
  int line = 1;
  int cnt = 0;
  size_t i = 0;
  QCString nr;
  while (i < size)
  {
    nr = "";
    // skip initial white space, but count lines
    while (i < size && (data[i] == ' ' || data[i] == '\t' || data[i] == '\n'))
    {
      if (data[i] == '\n') line++;
      i++;
    }
    if (i >= size) break;
    // check for \newcommand or \renewcommand
    if (data[i] != '\\')
    {
      warn(macrofile,line, "file contains non valid code, expected '\\' got '%c'\n",data[i]);
      return "";
    }
    i++;
    if (!qstrncmp(data + i, "newcommand", strlen("newcommand")))
    {
      i += strlen("newcommand");
    }
    else if (!qstrncmp(data + i, "renewcommand", strlen("renewcommand")))
    {
      i += strlen("renewcommand");
    }
    else
    {
      warn(macrofile,line, "file contains non valid code, expected 'newcommand' or 'renewcommand'");
      return "";
    }
    // handle {cmd}
    if (data[i] != '{')
    {
      warn(macrofile,line, "file contains non valid code, expected '{' got '%c'\n",data[i]);
      return "";
    }
    i++;
    if (data[i] != '\\')
    {
      warn(macrofile,line, "file contains non valid code, expected '\\' got '%c'\n",data[i]);
      return "";
    }
    i++;
    // run till }, i.e. cmd
    out.addStr("    ");
    while (i < size && (data[i] != '}')) out.addChar(data[i++]);
    if (i >= size)
    {
      warn(macrofile,line, "file contains non valid code, no closing '}' for command");
      return "";
    }
    out.addChar(':');
    out.addChar(' ');
    i++;

    if (data[i] == '[')
    {
      // handle [nr]
      // run till ]
      out.addChar('[');
      i++;
      while (i < size && (data[i] != ']')) nr += data[i++];
      if (i >= size)
      {
        warn(macrofile,line, "file contains non valid code, no closing ']'");
        return "";
      }
      i++;
    }
    else if (data[i] != '{')
    {
      warn(macrofile,line, "file contains non valid code, expected '[' or '{' got '%c'\n",data[i]);
      return "";
    }
    // handle {replacement}
    // retest as the '[' part might have advanced so we can have a new '{'
    if (data[i] != '{')
    {
      warn(macrofile,line, "file contains non valid code, expected '{' got '%c'\n",data[i]);
      return "";
    }
    out.addChar('"');
    out.addChar('{');
    i++;
    // run till }
    cnt = 1;
    while (i < size && cnt)
    {
      switch(data[i])
      {
        case '\\':
          out.addChar('\\'); // need to escape it for MathJax js code
          out.addChar('\\');
          i++;
          if (data[i] == '\\') // we have an escaped backslash
          {
            out.addChar('\\');
            out.addChar('\\');
            i++;
          }
          else if (data[i] != '"') out.addChar(data[i++]); // double quote handled separately
          break;
        case '{':
          cnt++;
          out.addChar(data[i++]);
          break;
        case '}':
          cnt--;
          if (cnt) out.addChar(data[i]);
          i++;
          break;
        case '"':
          out.addChar('\\'); // need to escape it for MathJax js code
          out.addChar(data[i++]);
          break;
        case '\n':
          line++;
          out.addChar(data[i++]);
          break;
        default:
          out.addChar(data[i++]);
          break;
      }
    }
    if (i > size)
    {
      warn(macrofile,line, "file contains non valid code, no closing '}' for replacement");
      return "";
    }
    out.addChar('}');
    out.addChar('"');
    if (!nr.isEmpty())
    {
      out.addChar(',');
      out.addStr(nr);
    }
    if (!nr.isEmpty())
    {
      out.addChar(']');
    }
    out.addChar(',');
    out.addChar('\n');
  }
  out.addChar(0);
  return out.get();
}

static QCString getSearchBox(bool serverSide, QCString relPath, bool highlightSearch)
{
  TextStream t;
  if (serverSide)
  {
    writeServerSearchBox(t, relPath, highlightSearch);
  }
  else
  {
    writeClientSearchBox(t, relPath);
  }
  return t.str();
}

static QCString substituteHtmlKeywords(const QCString &str,
                                       const QCString &title,
                                       const QCString &relPath,
                                       const QCString &navPath=QCString())
{
  // Build CSS/JavaScript tags depending on treeview, search engine settings
  QCString cssFile;
  QCString generatedBy;
  QCString treeViewCssJs;
  QCString searchCssJs;
  QCString searchBox;
  QCString mathJaxJs;
  QCString extraCssText;

  QCString projectName = Config_getString(PROJECT_NAME);
  bool timeStamp = Config_getBool(HTML_TIMESTAMP);
  bool treeView = Config_getBool(GENERATE_TREEVIEW);
  bool searchEngine = Config_getBool(SEARCHENGINE);
  bool serverBasedSearch = Config_getBool(SERVER_BASED_SEARCH);
  bool mathJax = Config_getBool(USE_MATHJAX);
  QCString mathJaxFormat = Config_getEnumAsString(MATHJAX_FORMAT);
  bool disableIndex = Config_getBool(DISABLE_INDEX);
  bool hasProjectName = !projectName.isEmpty();
  bool hasProjectNumber = !Config_getString(PROJECT_NUMBER).isEmpty();
  bool hasProjectBrief = !Config_getString(PROJECT_BRIEF).isEmpty();
  bool hasProjectLogo = !Config_getString(PROJECT_LOGO).isEmpty();
  bool hasFullSideBar = Config_getBool(FULL_SIDEBAR) && disableIndex && treeView;
  static bool titleArea = (hasProjectName || hasProjectBrief || hasProjectLogo || (disableIndex && searchEngine));

  cssFile = Config_getString(HTML_STYLESHEET);
  if (cssFile.isEmpty())
  {
    cssFile = "doxygen.css";
  }
  else
  {
    FileInfo cssfi(cssFile.str());
    if (cssfi.exists())
    {
      cssFile = cssfi.fileName();
    }
    else
    {
      cssFile = "doxygen.css";
    }
  }

  extraCssText = "";
  const StringVector &extraCssFile = Config_getList(HTML_EXTRA_STYLESHEET);
  for (const auto &fileName : extraCssFile)
  {
    if (!fileName.empty())
    {
      FileInfo fi(fileName);
      if (fi.exists())
      {
        extraCssText += "<link href=\"$relpath^"+stripPath(fileName.c_str())+"\" rel=\"stylesheet\" type=\"text/css\"/>\n";
      }
    }
  }

  if (timeStamp)
  {
    generatedBy = theTranslator->trGeneratedAt(dateToString(TRUE),
                                convertToHtml(Config_getString(PROJECT_NAME)));
  }
  else
  {
    generatedBy = theTranslator->trGeneratedBy();
  }

  if (treeView)
  {
    treeViewCssJs = "<link href=\"$relpath^navtree.css\" rel=\"stylesheet\" type=\"text/css\"/>\n"
    //                    "<script type=\"text/javascript\">var page_layout=";
    //treeViewCssJs += Config_getBool(DISABLE_INDEX) ? "1" : "0";
    //treeViewCssJs += ";</script>\n"
			"<script type=\"text/javascript\" src=\"$relpath^resize.js\"></script>\n"
			"<script type=\"text/javascript\" src=\"$relpath^navtreedata.js\"></script>\n"
			"<script type=\"text/javascript\" src=\"$relpath^navtree.js\"></script>\n";
  }

  if (searchEngine)
  {
    searchCssJs = "<link href=\"$relpath^search/search.css\" rel=\"stylesheet\" type=\"text/css\"/>\n";
    if (!serverBasedSearch)
    {
      searchCssJs += "<script type=\"text/javascript\" src=\"$relpath^search/searchdata.js\"></script>\n";
    }
    searchCssJs += "<script type=\"text/javascript\" src=\"$relpath^search/search.js\"></script>\n";

    if (!serverBasedSearch)
    {
      if (disableIndex || !Config_getBool(HTML_DYNAMIC_MENUS))
      {
        searchCssJs += "<script type=\"text/javascript\">\n"
					"/* @license magnet:?xt=urn:btih:d3d9a9a6595521f9666a5e94cc830dab83b65699&amp;dn=expat.txt MIT */\n"
				"  $(document).ready(function() { init_search(); });\n"
					"/* @license-end */\n"
					"</script>";
      }
    }
    else
    {
      if (disableIndex || !Config_getBool(HTML_DYNAMIC_MENUS))
      {
        searchCssJs += "<script type=\"text/javascript\">\n"
					"/* @license magnet:?xt=urn:btih:d3d9a9a6595521f9666a5e94cc830dab83b65699&amp;dn=expat.txt MIT */\n"
					"  $(document).ready(function() {\n"
					"    if ($('.searchresults').length > 0) { searchBox.DOMSearchField().focus(); }\n"
					"  });\n"
					"  /* @license-end */\n"
					"</script>\n";
      }

      // OPENSEARCH_PROVIDER {
      searchCssJs += "<link rel=\"search\" href=\"" + relPath +
                     "search_opensearch.php?v=opensearch.xml\" "
                     "type=\"application/opensearchdescription+xml\" title=\"" +
                     (hasProjectName ? projectName : QCString("Doxygen")) +
                     "\"/>";
      // OPENSEARCH_PROVIDER }
    }
    searchBox = getSearchBox(serverBasedSearch, relPath, FALSE);
  }

  if (mathJax)
  {
    auto mathJaxVersion = Config_getEnum(MATHJAX_VERSION);
    QCString path = Config_getString(MATHJAX_RELPATH);
    if (path.isEmpty() || path.left(2)=="..") // relative path
    {
      path.prepend(relPath);
    }

    switch (mathJaxVersion)
    {
      case MATHJAX_VERSION_t::MathJax_3:
        {
          mathJaxJs += "<script src=\"https://polyfill.io/v3/polyfill.min.js?features=es6\"></script>\n"
                       "<script type=\"text/javascript\">\n"
                       "window.MathJax = {\n"
                       "  options: {\n"
                       "    ignoreHtmlClass: 'tex2jax_ignore',\n"
                       "    processHtmlClass: 'tex2jax_process'\n"
                       "  }";
         const StringVector &mathJaxExtensions = Config_getList(MATHJAX_EXTENSIONS);
         if (!mathJaxExtensions.empty() || !g_latex_macro.isEmpty())
         {
           mathJaxJs+= ",\n"
                       "  tex: {\n"
                       "    macros: {";
           if (!g_latex_macro.isEmpty())
           {
             mathJaxJs += g_latex_macro+"    ";
           }
           mathJaxJs+="},\n"
                       "    packages: ['base','configmacros'";
           if (!g_latex_macro.isEmpty())
           {
             mathJaxJs+= ",'newcommand'";
           }
           for (const auto &s : mathJaxExtensions)
           {
             mathJaxJs+= ",'"+QCString(s.c_str())+"'";
           }
           mathJaxJs += "]\n"
                         "  }\n";
         }
         else
         {
           mathJaxJs += "\n";
         }
         mathJaxJs += "};\n";
         // MATHJAX_CODEFILE
         if (!g_mathjax_code.isEmpty())
         {
           mathJaxJs += g_mathjax_code;
           mathJaxJs += "\n";
         }
         mathJaxJs += "</script>\n";
         mathJaxJs += "<script type=\"text/javascript\" id=\"MathJax-script\" async=\"async\" src=\"" +
                      path + "es5/tex-" + mathJaxFormat.lower() + ".js\">";
         mathJaxJs+="</script>\n";
        }
        break;
      case MATHJAX_VERSION_t::MathJax_2:
        {
          mathJaxJs = "<script type=\"text/x-mathjax-config\">\n"
                      "MathJax.Hub.Config({\n"
                      "  extensions: [\"tex2jax.js\"";
          const StringVector &mathJaxExtensions = Config_getList(MATHJAX_EXTENSIONS);
          for (const auto &s : mathJaxExtensions)
          {
            mathJaxJs+= ", \""+QCString(s.c_str())+".js\"";
          }
          if (mathJaxFormat.isEmpty())
          {
            mathJaxFormat = "HTML-CSS";
          }
          mathJaxJs += "],\n"
                       "  jax: [\"input/TeX\",\"output/"+mathJaxFormat+"\"],\n";
          if (!g_latex_macro.isEmpty())
          {
            mathJaxJs += "   TeX: { Macros: {\n";
            mathJaxJs += g_latex_macro;
            mathJaxJs += "\n"
                         "  } }\n";
          }
          mathJaxJs +=   "});\n";
          if (!g_mathjax_code.isEmpty())
          {
            mathJaxJs += g_mathjax_code;
            mathJaxJs += "\n";
          }
          mathJaxJs += "</script>\n";
          mathJaxJs += "<script type=\"text/javascript\" async=\"async\" src=\"" + path + "MathJax.js\"></script>\n";
        }
        break;
    }
  }

  // first substitute generic keywords
  QCString result = substituteKeywords(str,title,
    convertToHtml(Config_getString(PROJECT_NAME)),
    convertToHtml(Config_getString(PROJECT_NUMBER)),
        convertToHtml(Config_getString(PROJECT_BRIEF)));

  // additional HTML only keywords
  result = substitute(result,"$navpath",navPath);
  result = substitute(result,"$stylesheet",cssFile);
  result = substitute(result,"$treeview",treeViewCssJs);
  result = substitute(result,"$searchbox",searchBox);
  result = substitute(result,"$search",searchCssJs);
  result = substitute(result,"$mathjax",mathJaxJs);
  result = substitute(result,"$generatedby",generatedBy);
  result = substitute(result,"$extrastylesheet",extraCssText);
  result = substitute(result,"$relpath$",relPath); //<-- obsolete: for backwards compatibility only
  result = substitute(result,"$relpath^",relPath); //<-- must be last

  // additional HTML only conditional blocks
  result = selectBlock(result,"FULL_SIDEBAR",hasFullSideBar,OutputGenerator::Html);
  result = selectBlock(result,"DISABLE_INDEX",disableIndex,OutputGenerator::Html);
  result = selectBlock(result,"GENERATE_TREEVIEW",treeView,OutputGenerator::Html);
  result = selectBlock(result,"SEARCHENGINE",searchEngine,OutputGenerator::Html);
  result = selectBlock(result,"TITLEAREA",titleArea,OutputGenerator::Html);
  result = selectBlock(result,"PROJECT_NAME",hasProjectName,OutputGenerator::Html);
  result = selectBlock(result,"PROJECT_NUMBER",hasProjectNumber,OutputGenerator::Html);
  result = selectBlock(result,"PROJECT_BRIEF",hasProjectBrief,OutputGenerator::Html);
  result = selectBlock(result,"PROJECT_LOGO",hasProjectLogo,OutputGenerator::Html);

  result = removeEmptyLines(result);

  return result;
}

//--------------------------------------------------------------------------

HtmlCodeGenerator::HtmlCodeGenerator(TextStream &t) : m_t(t)
{
}

HtmlCodeGenerator::HtmlCodeGenerator(TextStream &t,const QCString &relPath)
  : m_t(t), m_relPath(relPath)
{
}

void HtmlCodeGenerator::setRelativePath(const QCString &path)
{
  m_relPath = path;
}

struct CPrefixMap
{
  char const *m_pPrefix;
  char const *m_pClass;
  bool m_bVariable;
};

#define ignore(_Var)
static CPrefixMap g_PrefixMap[] =
  {
    {"t_", "highlight_template_non_type_param", true}                           ignore( t_Test )
    , {"tp_", "highlight_template_non_type_param_pack", true}                   ignore( tp_Test )
    , {"E", "highlight_enumerator", false}                                      ignore( ETest_Value ) // highlight_enum if it can be determined
    , {"c_", "highlight_constant_variable", true}                               ignore( c_Test )
    , {"gc_", "highlight_global_constant", true}                                ignore( gc_Test )
    , {"mc_", "highlight_member_constant_public", true}                         ignore( mc_Test )
    , {"mcp_", "highlight_member_constant_private", true}                       ignore( mcp_Test )
    , {"tf_", "highlight_function_template_non_type_param", true}               ignore( tf_Test )
    , {"tfp_", "highlight_function_template_non_type_param_pack", true}         ignore( tfp_Test )

    , {"N", "highlight_namespace", false}                                       ignore( NTest )

    , {"t_C", "highlight_template_type_param_class", false}                     ignore( t_CTest )
    , {"t_F", "highlight_template_type_param_function", false}                  ignore( t_FTest )
    , {"t_TC", "highlight_template_template_param", false}                      ignore( t_TCTest )
    , {"tp_C", "highlight_template_type_param_class_pack", false}               ignore( tp_CTest )
    , {"tp_F", "highlight_template_type_param_function_pack", false}            ignore( tp_FTest )
    , {"tp_TC", "highlight_template_template_param_pack", false}                ignore( tp_TCTest )
    , {"C", "highlight_type", false}                                            ignore( CTest )
    , {"F", "highlight_type_function", false}                                   ignore( FTest )
    , {"IC", "highlight_type_interface", false}                                 ignore( ICTest )
    , {"TC", "highlight_template_type", false}                                  ignore( TCTest )
    , {"TIC", "highlight_template_type_interface", false}                       ignore( TICTest )
    , {"tf_C", "highlight_function_template_type_param_class", false}           ignore( tf_CTest )
    , {"tf_F", "highlight_function_template_type_param_function", false}        ignore( tf_FTest )
    , {"tf_TC", "highlight_function_template_template_param", false}            ignore( tf_TCTest )
    , {"tfp_C", "highlight_function_template_type_param_class_pack", false}     ignore( tfp_CTest )
    , {"tfp_F", "highlight_function_template_type_param_function_pack", false}  ignore( tfp_FTest )
    , {"tfp_TC", "highlight_function_template_template_param_pack", false}      ignore( tfp_TCTest )

    , {"_f", "highlight_function_parameter_functor", false}                     ignore( _fTest )
    , {"p_f", "highlight_function_parameter_pack_functor", false}               ignore( p_fTest )
    , {"o_f", "highlight_function_parameter_output_functor", false}             ignore( o_fTest )
    , {"po_f", "highlight_function_parameter_output_pack_functor", false}       ignore( po_fTest )

    , {"_of", "highlight_function_parameter_output_functor", false}             ignore( _ofTest ) // Deprecate?
    , {"p_of", "highlight_function_parameter_output_pack_functor", false}       ignore( p_ofTest ) // Deprecate?

    , {"f", "highlight_variable_functor", false}                                ignore( fTest )
    , {"fl_", "highlight_variable_functor", false}                              ignore( fl_Test )// To be deprecated

    , {"m_f", "highlight_member_variable_public_functor", false}                ignore( m_fTest )
    , {"mp_f", "highlight_member_variable_private_functor", false}              ignore( mp_fTest )

    , {"f_", "highlight_member_function_public", false}                         ignore( f_Test )
    , {"fr_", "highlight_member_function_public_recursive", false}              ignore( fr_Test )
    , {"f_r", "highlight_member_function_public_recursive", false}              ignore( f_rTest )
    , {"fs_", "highlight_member_static_function_public", false}                 ignore( fs_Test )
    , {"fsr_", "highlight_member_static_function_public_recursive", false}      ignore( fsr_Test )
    , {"fs_r", "highlight_member_static_function_public_recursive", false}      ignore( fs_rTest )
    , {"fp_", "highlight_member_function_private", false}                       ignore( fp_Test )
    , {"fpr_", "highlight_member_function_private_recursive", false}            ignore( fpr_Test )
    , {"fp_r", "highlight_member_function_private_recursive", false}            ignore( fp_rTest )
    , {"fsp_", "highlight_member_static_function_private", false}               ignore( fsp_Test )
    , {"fspr_", "highlight_member_static_function_private_recursive", false}    ignore( fspr_Test )
    , {"fsp_r", "highlight_member_static_function_private_recursive", false}    ignore( fsp_rTest )
    , {"fg_", "highlight_function", false}                                      ignore( fg_Test )
    , {"fgr_", "highlight_function_recursive", false}                           ignore( fgr_Test )
    , {"fg_r", "highlight_function_recursive", false}                           ignore( fg_rTest )
    , {"fsg_", "highlight_static_function", false}                              ignore( fsg_Test )
    , {"fsgr_", "highlight_static_function_recursive", false}                   ignore( fsgr_Test )
    , {"fsg_r", "highlight_static_function_recursive", false}                   ignore( fsg_rTest )

    , {"_", "highlight_function_parameter", true}                               ignore( _Test )
    , {"p_", "highlight_function_parameter_pack", true}                         ignore( p_Test )
    , {"o_", "highlight_function_parameter_output", true}                       ignore( o_Test )
    , {"po_", "highlight_function_parameter_output_pack", true}                 ignore( po_Test )


    , {"_o", "highlight_function_paramater_output", true}                       ignore( _oTest ) // Deprecate?
    , {"p_o", "highlight_function_parameter_output_pack", true}                 ignore( p_oTest ) // Deprecate?


    , {"m_", "highlight_member_variable_public", true}                          ignore( m_Test )
    , {"mp_", "highlight_member_variable_private", true}                        ignore( mp_Test )

    , {"D", "highlight_macro", false}                                           ignore( DTest )
    , {"d_", "highlight_macro_parameter", true}                                 ignore( d_Test )

    , {"ms_", "highlight_member_static_variable_public", true}                  ignore( ms_Test )
    , {"ms_f", "highlight_member_static_variable_public_functor", false}        ignore( ms_fTest )
    , {"msp_", "highlight_member_static_variable_private", true}                ignore( msp_Test )
    , {"msp_f", "highlight_member_static_variable_private_functor", false}      ignore( msp_fTest )

    , {"gs_", "highlight_global_static_variable", true}                         ignore( gs_Test )
    , {"gs_f", "highlight_global_static_variable_functor", false}               ignore( gs_fTest )
    , {"g_", "highlight_global_variable", true}                                 ignore( g_Test )
    , {"g_f", "highlight_global_variable_functor", false}                       ignore( g_fTest )
    , {"s_", "highlight_static_variable", true}                                 ignore( s_Test )
    , {"s_f", "highlight_static_variable_functor", false}                       ignore( s_fTest )
  }
;


struct CKeywordMap
{
  char const *m_pKeyword;
  char const *m_pClass;
};

static CKeywordMap g_KeywordMap[] =
  {
    // Qualifiers
    {"const", "highlight_keyword_qualifier"}
    , {"volatile", "highlight_keyword_qualifier"}

    // Storage class
    , {"register", "highlight_keyword_storage_class"}
    , {"static", "highlight_keyword_storage_class"}
    , {"extern", "highlight_keyword_storage_class"}
    , {"mutable", "highlight_keyword_storage_class"}

    // built in types
    , {"bool", "highlight_keyword_built_in_type"}
    , {"void", "highlight_keyword_built_in_type"}
    , {"bint", "highlight_keyword_built_in_type"}
    , {"zbint", "highlight_keyword_built_in_type"}
    , {"zbool", "highlight_keyword_built_in_type"}

    // built in character types
    , {"char", "highlight_keyword_built_in_character_type"}
    , {"__wchar_t", "highlight_keyword_built_in_character_type"}
    , {"wchar_t", "highlight_keyword_built_in_character_type"}
    , {"ch8", "highlight_keyword_built_in_character_type"}
    , {"ch16", "highlight_keyword_built_in_character_type"}
    , {"ch32", "highlight_keyword_built_in_character_type"}
    , {"uch8", "highlight_keyword_built_in_character_type"}
    , {"uch16", "highlight_keyword_built_in_character_type"}
    , {"uch32", "highlight_keyword_built_in_character_type"}

    , {"zch8", "highlight_keyword_built_in_character_type"}
    , {"zch16", "highlight_keyword_built_in_character_type"}
    , {"zch32", "highlight_keyword_built_in_character_type"}
    , {"zuch8", "highlight_keyword_built_in_character_type"}
    , {"zuch16", "highlight_keyword_built_in_character_type"}
    , {"zuch32", "highlight_keyword_built_in_character_type"}
    , {"char16_t", "highlight_keyword_built_in_character_type"}
    , {"char32_t", "highlight_keyword_built_in_character_type"}
    , {"zuch32", "highlight_keyword_built_in_character_type"}


    // built in integer types
    , {"int", "highlight_keyword_built_in_integer_type"}
    , {"size_t", "highlight_keyword_built_in_integer_type"}
    , {"__int16", "highlight_keyword_built_in_integer_type"}
    , {"__int32", "highlight_keyword_built_in_integer_type"}
    , {"__int64", "highlight_keyword_built_in_integer_type"}
    , {"__int8", "highlight_keyword_built_in_integer_type"}

    , {"int8", "highlight_keyword_built_in_integer_type"}
    , {"int16", "highlight_keyword_built_in_integer_type"}
    , {"int32", "highlight_keyword_built_in_integer_type"}
    , {"int64", "highlight_keyword_built_in_integer_type"}
    , {"int80", "highlight_keyword_built_in_integer_type"}
    , {"int128", "highlight_keyword_built_in_integer_type"}
    , {"int160", "highlight_keyword_built_in_integer_type"}
    , {"int256", "highlight_keyword_built_in_integer_type"}
    , {"int512", "highlight_keyword_built_in_integer_type"}
    , {"int1024", "highlight_keyword_built_in_integer_type"}
    , {"int2048", "highlight_keyword_built_in_integer_type"}
    , {"int4096", "highlight_keyword_built_in_integer_type"}
    , {"int8192", "highlight_keyword_built_in_integer_type"}

    , {"uint8", "highlight_keyword_built_in_integer_type"}
    , {"uint16", "highlight_keyword_built_in_integer_type"}
    , {"uint32", "highlight_keyword_built_in_integer_type"}
    , {"uint64", "highlight_keyword_built_in_integer_type"}
    , {"uint80", "highlight_keyword_built_in_integer_type"}
    , {"uint128", "highlight_keyword_built_in_integer_type"}
    , {"uint160", "highlight_keyword_built_in_integer_type"}
    , {"uint256", "highlight_keyword_built_in_integer_type"}
    , {"uint512", "highlight_keyword_built_in_integer_type"}
    , {"uint1024", "highlight_keyword_built_in_integer_type"}
    , {"uint2048", "highlight_keyword_built_in_integer_type"}
    , {"uint4096", "highlight_keyword_built_in_integer_type"}
    , {"uint8192", "highlight_keyword_built_in_integer_type"}

    , {"zint8", "highlight_keyword_built_in_integer_type"}
    , {"zuint8", "highlight_keyword_built_in_integer_type"}
    , {"zint16", "highlight_keyword_built_in_integer_type"}
    , {"zuint16", "highlight_keyword_built_in_integer_type"}
    , {"zint32", "highlight_keyword_built_in_integer_type"}
    , {"zuint32", "highlight_keyword_built_in_integer_type"}
    , {"zint64", "highlight_keyword_built_in_integer_type"}
    , {"zuint64", "highlight_keyword_built_in_integer_type"}
    , {"zint80", "highlight_keyword_built_in_integer_type"}
    , {"zuint80", "highlight_keyword_built_in_integer_type"}
    , {"zint128", "highlight_keyword_built_in_integer_type"}
    , {"zuint128", "highlight_keyword_built_in_integer_type"}
    , {"zint160", "highlight_keyword_built_in_integer_type"}
    , {"zuint160", "highlight_keyword_built_in_integer_type"}
    , {"zint256", "highlight_keyword_built_in_integer_type"}
    , {"zuint256", "highlight_keyword_built_in_integer_type"}
    , {"zint512", "highlight_keyword_built_in_integer_type"}
    , {"zuint512", "highlight_keyword_built_in_integer_type"}
    , {"zint1024", "highlight_keyword_built_in_integer_type"}
    , {"zuint1024", "highlight_keyword_built_in_integer_type"}
    , {"zint2048", "highlight_keyword_built_in_integer_type"}
    , {"zuint2048", "highlight_keyword_built_in_integer_type"}
    , {"zint4096", "highlight_keyword_built_in_integer_type"}
    , {"zuint4096", "highlight_keyword_built_in_integer_type"}
    , {"zint8192", "highlight_keyword_built_in_integer_type"}
    , {"zuint8192", "highlight_keyword_built_in_integer_type"}

    , {"mint", "highlight_keyword_built_in_integer_type"}
    , {"smint", "highlight_keyword_built_in_integer_type"}
    , {"umint", "highlight_keyword_built_in_integer_type"}
    , {"aint", "highlight_keyword_built_in_integer_type"}
    , {"uaint", "highlight_keyword_built_in_integer_type"}

    , {"zmint", "highlight_keyword_built_in_integer_type"}
    , {"zumint", "highlight_keyword_built_in_integer_type"}
    , {"zsmint", "highlight_keyword_built_in_integer_type"}
    , {"zamint", "highlight_keyword_built_in_integer_type"}
    , {"zuamint", "highlight_keyword_built_in_integer_type"}


    // builtin type modifiers
    , {"long", "highlight_keyword_built_in_type_modifier"}
    , {"short", "highlight_keyword_built_in_type_modifier"}
    , {"signed", "highlight_keyword_built_in_type_modifier"}
    , {"unsigned", "highlight_keyword_built_in_type_modifier"}

    // built in vector types
    , {"__m128", "highlight_keyword_built_in_vector_type"}
    , {"__m64", "highlight_keyword_built_in_vector_type"}
    , {"__w64", "highlight_keyword_built_in_vector_type"}
    , {"__m128i", "highlight_keyword_built_in_vector_type"}
    , {"__m128d", "highlight_keyword_built_in_vector_type"}

    // built in floating point types
    , {"float", "highlight_keyword_built_in_float_type"}
    , {"double", "highlight_keyword_built_in_float_type"}

    , {"fp8", "highlight_keyword_built_in_float_type"}
    , {"fp16", "highlight_keyword_built_in_float_type"}
    , {"fp32", "highlight_keyword_built_in_float_type"}
    , {"fp64", "highlight_keyword_built_in_float_type"}
    , {"fp80", "highlight_keyword_built_in_float_type"}
    , {"fp128", "highlight_keyword_built_in_float_type"}
    , {"fp256", "highlight_keyword_built_in_float_type"}
    , {"fp512", "highlight_keyword_built_in_float_type"}
    , {"fp1024", "highlight_keyword_built_in_float_type"}
    , {"fp2048", "highlight_keyword_built_in_float_type"}
    , {"fp4096", "highlight_keyword_built_in_float_type"}
    , {"ufp8", "highlight_keyword_built_in_float_type"}
    , {"ufp16", "highlight_keyword_built_in_float_type"}
    , {"ufp32", "highlight_keyword_built_in_float_type"}
    , {"ufp64", "highlight_keyword_built_in_float_type"}
    , {"ufp80", "highlight_keyword_built_in_float_type"}
    , {"ufp128", "highlight_keyword_built_in_float_type"}
    , {"ufp256", "highlight_keyword_built_in_float_type"}
    , {"ufp512", "highlight_keyword_built_in_float_type"}
    , {"ufp1024", "highlight_keyword_built_in_float_type"}
    , {"ufp2048", "highlight_keyword_built_in_float_type"}
    , {"ufp4096", "highlight_keyword_built_in_float_type"}

    , {"zfp8", "highlight_keyword_built_in_float_type"}
    , {"zfp16", "highlight_keyword_built_in_float_type"}
    , {"zfp32", "highlight_keyword_built_in_float_type"}
    , {"zfp64", "highlight_keyword_built_in_float_type"}
    , {"zfp80", "highlight_keyword_built_in_float_type"}
    , {"zfp128", "highlight_keyword_built_in_float_type"}
    , {"zfp256", "highlight_keyword_built_in_float_type"}
    , {"zfp512", "highlight_keyword_built_in_float_type"}
    , {"zfp1024", "highlight_keyword_built_in_float_type"}
    , {"zfp2048", "highlight_keyword_built_in_float_type"}
    , {"zfp4096", "highlight_keyword_built_in_float_type"}
    , {"zufp8", "highlight_keyword_built_in_float_type"}
    , {"zufp16", "highlight_keyword_built_in_float_type"}
    , {"zufp32", "highlight_keyword_built_in_float_type"}
    , {"zufp64", "highlight_keyword_built_in_float_type"}
    , {"zufp80", "highlight_keyword_built_in_float_type"}
    , {"zufp128", "highlight_keyword_built_in_float_type"}
    , {"zufp256", "highlight_keyword_built_in_float_type"}
    , {"zufp512", "highlight_keyword_built_in_float_type"}
    , {"zufp1024", "highlight_keyword_built_in_float_type"}
    , {"zufp2048", "highlight_keyword_built_in_float_type"}
    , {"zufp4096", "highlight_keyword_built_in_float_type"}

    // bult in constants
    , {"false", "highlight_keyword_built_in_constant"}
    , {"true", "highlight_keyword_built_in_constant"}
    , {"nullptr", "highlight_keyword_built_in_constant"}
    , {"NULL", "highlight_keyword_built_in_constant"}


    // Exception handling
    , {"try", "highlight_keyword_exception_handling"}
    , {"throw", "highlight_keyword_exception_handling"}
    , {"catch", "highlight_keyword_exception_handling"}
    , {"__try", "highlight_keyword_exception_handling"}
    , {"__except", "highlight_keyword_exception_handling"}
    , {"__finally", "highlight_keyword_exception_handling"}
    , {"__leave", "highlight_keyword_exception_handling"}
    , {"__raise", "highlight_keyword_exception_handling"}
    , {"finally", "highlight_keyword_exception_handling"}

    // Type introspection/type traits
    , {"__alignof", "highlight_keyword_introspection"}
    , {"sizeof", "highlight_keyword_introspection"}
    , {"decltype", "highlight_keyword_introspection"}
    , {"__uuidof", "highlight_keyword_introspection"}
    , {"typeid", "highlight_keyword_introspection"}

    // Static assert
    , {"static_assert", "highlight_keyword_static_assert"}

    // Control statements
    , {"while", "highlight_keyword_control_statement"}
    , {"for", "highlight_keyword_control_statement"}
    , {"goto", "highlight_keyword_control_statement"}
    , {"if", "highlight_keyword_control_statement"}
    , {"do", "highlight_keyword_control_statement"}
    , {"break", "highlight_keyword_control_statement"}
    , {"case", "highlight_keyword_control_statement"}
    , {"continue", "highlight_keyword_control_statement"}
    , {"default", "highlight_keyword_control_statement"}
    , {"else", "highlight_keyword_control_statement"}
    , {"return", "highlight_keyword_control_statement"}
    , {"switch", "highlight_keyword_control_statement"}

    , {"co_await", "highlight_keyword_control_statement"}
    , {"co_return", "highlight_keyword_control_statement"}
    , {"co_yield", "highlight_keyword_control_statement"}

    , {"likely", "highlight_keyword_control_statement"}
    , {"unlikely", "highlight_keyword_control_statement"}
    , {"assume", "highlight_keyword_control_statement"}

    , {"yield_cpu", "highlight_keyword_control_statement"}

    , {"constant_int64", "highlight_keyword_control_statement"}
    , {"constant_uint64", "highlight_keyword_control_statement"}

    // Optimization
    , {"__asm", "highlight_keyword_optimization"}
    , {"__assume", "highlight_keyword_optimization"}

    // Property modifiers
    , {"__unaligned", "highlight_keyword_property_modifier"}
    , {"__declspec", "highlight_keyword_property_modifier"}
    , {"__based", "highlight_keyword_property_modifier"}
    , {"deprecated", "highlight_keyword_property_modifier"}
    , {"dllexport", "highlight_keyword_property_modifier"}
    , {"dllimport", "highlight_keyword_property_modifier"}
    , {"naked", "highlight_keyword_property_modifier"}
    , {"noinline", "highlight_keyword_property_modifier"}
    , {"noreturn", "highlight_keyword_property_modifier"}
    , {"nothrow", "highlight_keyword_property_modifier"}
    , {"noexcept", "highlight_keyword_property_modifier"}
    , {"novtable", "highlight_keyword_property_modifier"}
    , {"property", "highlight_keyword_property_modifier"}
    , {"selectany", "highlight_keyword_property_modifier"}
    , {"thread", "highlight_keyword_property_modifier"}
    , {"uuid", "highlight_keyword_property_modifier"}
    , {"explicit", "highlight_keyword_property_modifier"}
    , {"__forceinline", "highlight_keyword_property_modifier"}
    , {"__inline", "highlight_keyword_property_modifier"}
    , {"inline", "highlight_keyword_property_modifier"}
    , {"__cdecl", "highlight_keyword_property_modifier"}
    , {"__thiscall", "highlight_keyword_property_modifier"}
    , {"__fastcall", "highlight_keyword_property_modifier"}
    , {"__stdcall", "highlight_keyword_property_modifier"}
    , {"calling_convention_c", "highlight_keyword_property_modifier"}
    , {"cdecl", "highlight_keyword_property_modifier"}
    , {"stdcall", "highlight_keyword_property_modifier"}
    , {"fastcall", "highlight_keyword_property_modifier"}
    , {"inline_small", "highlight_keyword_property_modifier"}
    , {"inline_always", "highlight_keyword_property_modifier"}
    , {"inline_never", "highlight_keyword_property_modifier"}
    , {"inline_never_debug", "highlight_keyword_property_modifier"}
    , {"inline_medium", "highlight_keyword_property_modifier"}
    , {"inline_large", "highlight_keyword_property_modifier"}
    , {"inline_extralarge", "highlight_keyword_property_modifier"}
    , {"inline_always_debug", "highlight_keyword_property_modifier"}
    , {"module_export", "highlight_keyword_property_modifier"}
    , {"module_import", "highlight_keyword_property_modifier"}
    , {"only_parameters_aliased", "highlight_keyword_property_modifier"}
    , {"return_not_aliased", "highlight_keyword_property_modifier"}
    , {"function_does_not_return", "highlight_keyword_property_modifier"}
    , {"variable_not_aliased", "highlight_keyword_property_modifier"}
    , {"constexpr", "highlight_keyword_property_modifier"}
    , {"__pragma", "highlight_keyword_property_modifier"}
    , {"__attribute__", "highlight_keyword_property_modifier"}
    , {"__restrict__", "highlight_keyword_property_modifier"}
    , {"assure_used", "highlight_keyword_property_modifier"}
    , {"align_cacheline", "highlight_keyword_property_modifier"}
    , {"likely", "highlight_keyword_property_modifier"}
    , {"unlikely", "highlight_keyword_property_modifier"}
    , {"intrinsic", "highlight_keyword_property_modifier"}

    // new/delete operators
    , {"delete", "highlight_keyword_new_delete"}
    , {"new", "highlight_keyword_new_delete"}

    // CLR
    , {"__abstract", "highlight_keyword_clr"}
    , {"abstract", "highlight_keyword_clr"}
    , {"__box", "highlight_keyword_clr"}
    , {"__delegate", "highlight_keyword_clr"}
    , {"__gc", "highlight_keyword_clr"}
    , {"__hook", "highlight_keyword_clr"}
    , {"__nogc", "highlight_keyword_clr"}
    , {"__pin", "highlight_keyword_clr"}
    , {"__property", "highlight_keyword_clr"}
    , {"__sealed", "highlight_keyword_clr"}
    , {"__try_cast", "highlight_keyword_clr"}
    , {"__unhook", "highlight_keyword_clr"}
    , {"__value", "highlight_keyword_clr"}
    , {"event", "highlight_keyword_clr"}
    , {"__identifier", "highlight_keyword_clr"}
    , {"friend_as", "highlight_keyword_clr"}
    , {"interface", "highlight_keyword_clr"}
    , {"interior_ptr", "highlight_keyword_clr"}
    , {"gcnew", "highlight_keyword_clr"}
    , {"generic", "highlight_keyword_clr"}
    , {"initonly", "highlight_keyword_clr"}
    , {"literal", "highlight_keyword_clr"}
    , {"ref", "highlight_keyword_clr"}
    , {"safecast", "highlight_keyword_clr"}

    // Other keywords
    , {"__event", "highlight_keyword_other"}
    , {"__if_exists", "highlight_keyword_other"}
    , {"__if_not_exists", "highlight_keyword_other"}
    , {"__interface", "highlight_keyword_other"}
    , {"__multiple_inheritance", "highlight_keyword_other"}
    , {"__single_inheritance", "highlight_keyword_other"}
    , {"__virtual_inheritance", "highlight_keyword_other"}
    , {"__super", "highlight_keyword_other"}
    , {"__noop", "highlight_keyword_other"}

    // Type specification keywords
    , {"union", "highlight_keyword_type_specification"}
    , {"class", "highlight_keyword_type_specification"}
    , {"enum", "highlight_keyword_type_specification"}
    , {"struct", "highlight_keyword_type_specification"}

    // namespace
    , {"namespace", "highlight_keyword_namespace"}

    // typename
    , {"typename", "highlight_keyword_typename"}

    // template
    , {"template", "highlight_keyword_template"}

    // typedef
    , {"typedef", "highlight_keyword_typedef"}

    // using
    , {"using", "highlight_keyword_using"}

    // auto
    , {"auto", "highlight_keyword_auto"}

    // this
    , {"this", "highlight_keyword_this"}

    // operator
    , {"operator", "highlight_keyword_operator"}

    // Access keywords
    , {"friend", "highlight_keyword_access"}
    , {"private", "highlight_keyword_access"}
    , {"public", "highlight_keyword_access"}
    , {"protected", "highlight_keyword_access"}

    // Virtual keywords
    , {"final", "highlight_keyword_virtual"}
    , {"sealed", "highlight_keyword_virtual"}
    , {"override", "highlight_keyword_virtual"}
    , {"virtual", "highlight_keyword_virtual"}
    , {"pure", "highlight_keyword_pure"}

    // casts
    , {"const_cast", "highlight_keyword_casts"}
    , {"dynamic_cast", "highlight_keyword_casts"}
    , {"reinterpret_cast", "highlight_keyword_casts"}
    , {"static_cast", "highlight_keyword_casts"}

    // stl forced functionality
    , {"std", "highlight_namespace"}
    , {"experimental", "highlight_namespace"}

    , {"coroutine_traits", "highlight_type"}
    , {"coroutine_handle", "highlight_type"}
    , {"suspend_always", "highlight_type"}
    , {"suspend_never", "highlight_type"}
    , {"promise_type", "highlight_type"}
    , {"noop_coroutine_promise", "highlight_type"}
    , {"noop_coroutine_handle", "highlight_type"}
    , {"exception_ptr", "highlight_type"}

    , {"pair", "highlight_template_type"}
    , {"vector", "highlight_template_type"}
    , {"list", "highlight_template_type"}
    , {"slist", "highlight_template_type"}
    , {"deque", "highlight_template_type"}
    , {"priority_queue", "highlight_template_type"}
    , {"stack", "highlight_template_type"}
    , {"set", "highlight_template_type"}
    , {"multiset", "highlight_template_type"}
    , {"map", "highlight_template_type"}
    , {"multimap", "highlight_template_type"}
    , {"hash_set", "highlight_template_type"}
    , {"hash_multiset", "highlight_template_type"}
    , {"hash_map", "highlight_template_type"}
    , {"hash_multimap", "highlight_template_type"}
    , {"bitset", "highlight_template_type"}
    , {"valarray", "highlight_template_type"}

    , {"begin", "highlight_member_function_public"}
    , {"end", "highlight_member_function_public"}

    , {"await_transform", "highlight_member_function_public"}
    , {"await_ready", "highlight_member_function_public"}
    , {"await_suspend", "highlight_member_function_public"}
    , {"await_resume", "highlight_member_function_public"}

    , {"get_return_object", "highlight_member_function_public"}
    , {"return_value", "highlight_member_function_public"}
    , {"return_void", "highlight_member_function_public"}
    , {"yield_value", "highlight_member_function_public"}
    , {"initial_suspend", "highlight_member_function_public"}
    , {"final_suspend", "highlight_member_function_public"}
    , {"unhandled_exception", "highlight_member_function_public"}

    , {"address", "highlight_member_function_public"}
    , {"resume", "highlight_member_function_public"}
    , {"destroy", "highlight_member_function_public"}
    , {"done", "highlight_member_function_public"}
    , {"promise", "highlight_member_function_public"}

    , {"from_promise", "highlight_member_static_function_public"}

    , {"rethrow_exception", "highlight_function"}
    , {"uncaught_exceptions", "highlight_function"}
    , {"current_exception", "highlight_function"}
    , {"make_exception_ptr", "highlight_function"}

    , {"make_exception_ptr", "highlight_function"}

    , {"constant_int64", "highlight_macro"}
    , {"constant_uint64", "highlight_macro"}
    , {"str_utf8", "highlight_macro"}
    , {"str_utf16", "highlight_macro"}
    , {"str_utf32", "highlight_macro"}

  }
;
static CKeywordMap g_KeywordMapPreprocessor[] =
  {
    {"define", "highlight_keyword_preprocessor_directive"}
    , {"error", "highlight_keyword_preprocessor_directive"}
    , {"import", "highlight_keyword_preprocessor_directive"}
    , {"undef", "highlight_keyword_preprocessor_directive"}
    , {"elif", "highlight_keyword_preprocessor_directive"}
    , {"if", "highlight_keyword_preprocessor_directive"}
    , {"include", "highlight_keyword_preprocessor_directive"}
    , {"using", "highlight_keyword_preprocessor_directive"}
    , {"else", "highlight_keyword_preprocessor_directive"}
    , {"ifdef", "highlight_keyword_preprocessor_directive"}
    , {"line", "highlight_keyword_preprocessor_directive"}
    , {"endif", "highlight_keyword_preprocessor_directive"}
    , {"ifndef", "highlight_keyword_preprocessor_directive"}
    , {"pragma", "highlight_keyword_preprocessor_directive"}
    , {"once", "highlight_keyword_preprocessor_directive"}
  }
;

static char const g_Charset_Whitespace[] = "\r\n\t\v\b\f\a ";

static char const g_Charset_Operators[] = ":;+-()[]{}<>!~*&.,/%=^|?";

static char const g_Charset_StartIdentifier[] = "_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
static char const g_Charset_Identifier[] = "_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

static char const g_Charset_Alpha[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";


static char const g_Charset_Number[] = "0123456789";
static char const g_Charset_NumberHex[] = "0123456789abcdefABCDEF";

static char const g_Charset_ValidConcept[] = "bcfinpt";

static char const g_Charset_PreprocessorOperator[] = "#";
static char const g_Charset_PreprocessorEscape[] = "\\";

static char const g_Charset_UpperCaseChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

bool fg_CharacterInSet(char _Character, char const *_pSet)
{
  char const *pSet = _pSet;
  while (*pSet)
  {
    if (*pSet == _Character)
      return true;
    ++pSet;
  }
  return false;
}

const static int gc_MaxPrefixLen = 6;

static QMap<QString, CPrefixMap *> g_PrefixMapInfoVar[gc_MaxPrefixLen + 1];
static QMap<QString, CPrefixMap *> g_PrefixMapInfo[gc_MaxPrefixLen + 1];

static QMap<QString, CKeywordMap *> g_KeywordMapInfo;
static QMap<QString, CKeywordMap *> g_KeywordMapPreprocessorInfo;

static bool g_bPrefixMapCreated = false;

static void fg_CreateKeywordMap()
{
  {
    int nKeywords = sizeof(g_KeywordMap) / sizeof(g_KeywordMap[0]);
    for (int i = 0; i < nKeywords; ++i)
    {
      CKeywordMap *pPrefix = &g_KeywordMap[i];
      g_KeywordMapInfo.insert(pPrefix->m_pKeyword, pPrefix);
    }
  }

  {
    int nKeywords = sizeof(g_KeywordMapPreprocessor) / sizeof(g_KeywordMapPreprocessor[0]);
    for (int i = 0; i < nKeywords; ++i)
    {
      CKeywordMap *pPrefix = &g_KeywordMapPreprocessor[i];
      g_KeywordMapPreprocessorInfo.insert(pPrefix->m_pKeyword, pPrefix);
    }
  }
}

static void fg_CreatePrefixMap()
{
  g_bPrefixMapCreated = true;
  fg_CreateKeywordMap();
  int nPrefixes = sizeof(g_PrefixMap) / sizeof(g_PrefixMap[0]);

  for (int i = 0; i < nPrefixes; ++i)
  {
    CPrefixMap *pPrefix = &g_PrefixMap[i];

    size_t PrefixLen = strlen(pPrefix->m_pPrefix);

    QMap<QString, CPrefixMap *> *pMapInfo = NULL;
    if (pPrefix->m_bVariable)
      pMapInfo = &g_PrefixMapInfoVar[PrefixLen];
    else
      pMapInfo = &g_PrefixMapInfo[PrefixLen];

    pMapInfo->insert(pPrefix->m_pPrefix, pPrefix);
  }
}

static bool fg_MatchVariablePrefix(QString const &_Identifier, QString const &_ToMatch, size_t _MatchLength, size_t _IdentLength)
{
  if (!_Identifier.startsWith(_ToMatch))
    return false;

  size_t MatchLength = _MatchLength;
  size_t IdentLength = _IdentLength;
  if (IdentLength <= MatchLength)
    return false;

  QChar Character = _Identifier[MatchLength];

  if (fg_CharacterInSet(Character, g_Charset_UpperCaseChars))
    return true;


  if (!fg_CharacterInSet(Character, g_Charset_ValidConcept))
    return false;

  if (IdentLength <= MatchLength + 1)
    return false;

  Character = _Identifier[MatchLength + 1];

  if (fg_CharacterInSet(Character, g_Charset_UpperCaseChars))
    return true;

  return false;
}

static bool fg_MatchOtherPrefix(QString const &_Identifier, QString const &_ToMatch, size_t _MatchLength, size_t _IdentLength)
{
  if (_IdentLength <= _MatchLength)
    return false;
  if (_Identifier.startsWith(_ToMatch) && fg_CharacterInSet(_Identifier[_ToMatch.length()], g_Charset_UpperCaseChars))
    return true;
  return false;
}

void HtmlCodeGenerator::handleDeferredCodify()
{
  if (m_deferredCodify.empty())
    return;

  char const *str = m_deferredCodify.c_str();

  if (!g_bPrefixMapCreated)
    fg_CreatePrefixMap();

  int tabSize = Config_getInt(TAB_SIZE);
  if (str))
  {
    if (m_inDocumentationComment && !m_inCommentStart && !m_inComment)
      m_inDocumentationComment = false;

    const char *p=str;
    char c;
    int spacesToNextTabStop;
    while (*p)
    {
      c=*p++;
      switch(c)
      {
        case '\t': spacesToNextTabStop =
                         tabSize - (m_col%tabSize);
                   m_t << Doxygen::spaces.left(spacesToNextTabStop);
                   m_col+=spacesToNextTabStop;
                   break;
        case '\n': m_t << "\n"; m_col=0;
                   break;
        case '\r': break;
        case '<':
          if (m_inComment || m_inChar || m_inString || m_inPreprocessor)
            m_t << "&lt;";
          else
            m_t << "<span class=\"highlight_operator\">&lt;</span>";
          m_col++;
          break;
        case '>':
          if (m_inComment || m_inChar || m_inString || m_inPreprocessor)
            m_t << "&gt;";
          else
            m_t << "<span class=\"highlight_operator\">&gt;</span>";
          m_col++; 
          break;
        case '&':
          if (m_inComment || m_inChar || m_inString || m_inPreprocessor)
            m_t << "&amp;";
          else
            m_t << "<span class=\"highlight_operator\">&amp;</span>";
          m_col++;
          break;
        case '\'': m_t << "&#39;"; m_col++; // &apos; is not valid XHTML
                   break;
        case '"':  m_t << "&quot;"; m_col++;
                   break;
        case '\\':
                   if (*p=='<')
                     { m_t << "&lt;"; p++; }
                   else if (*p=='>')
                     { m_t << "&gt;"; p++; }
		   else if (*p=='(')
                     { m_t << "\\&zwj;("; m_col++;p++; }
                   else if (*p==')')
                     { m_t << "\\&zwj;)"; m_col++;p++; }
                   else
                     m_t << "\\";
                   m_col++;
                   break;
        default:
          {
            if (m_inChar || m_inString)
            {
              uchar uc = static_cast<uchar>(c);
              if (uc<32)
              {
                m_t << "&#x24" << hex[uc>>4] << hex[uc&0xF] << ";";
                m_col++;
              }
              else
              {
                p=writeUTF8Char(m_t,p-1);
                m_col++;
              }
              break;
            }

            if (!m_inComment && (fg_CharacterInSet(c, g_Charset_Number) || (c == '.' && fg_CharacterInSet(*p, g_Charset_Number))))
            {
              bool bFoundNumber = false;
              char const *pStart = p-1;
              char const *pParse = p-1;
              do
              {
                while (*pParse && fg_CharacterInSet(*pParse, g_Charset_Number))
                  ++pParse;

                bool bIsFloating = false;
                if (*pParse == '.')
                {
                  bIsFloating = true;
                  ++pParse;
                  while (*pParse && fg_CharacterInSet(*pParse, g_Charset_Number))
                    ++pParse;
                }
                else if (*pParse == 'E' || *pParse == 'e')
                  bIsFloating = true;

                if (bIsFloating)
                {
                  if (*pParse == 'E' || *pParse == 'e')
                  {
                    ++pParse;
                    if (*pParse == '+' || *pParse == '-')
                      ++pParse;
                    if (!fg_CharacterInSet(*pParse, g_Charset_Number))
                      break; // Invalid number
                    while (*pParse && fg_CharacterInSet(*pParse, g_Charset_Number))
                      ++pParse;
                  }

                  // Parse any suffixes
                  while (fg_CharacterInSet(*pParse, g_Charset_Alpha))
                    ++pParse;

                  bFoundNumber = true;
                }
                else
                {
                  if (*pParse == 'X' || *pParse == 'x')
                  {
                    // Hex
                    ++pParse;
                    if (!fg_CharacterInSet(*pParse, g_Charset_NumberHex))
                      break; // Invalid number
                    while (fg_CharacterInSet(*pParse, g_Charset_NumberHex))
                      ++pParse;

                  }

                  // Parse any suffixes
                  while (fg_CharacterInSet(*pParse, g_Charset_Alpha))
                    ++pParse;

                  bFoundNumber = true;
                }
              }
              while (false)
                ;

              if (bFoundNumber)
              {
                m_t << "<span class=\"highlight_number\">";
                char const *pCopy = pStart;
                while (pCopy != pParse)
                {
                  m_t << *pCopy;
                  m_col++;
                  ++pCopy;
                }
                p = pCopy;
                m_t << "</span>";
                break;
              }
            }

            if (!m_inComment && !m_inPreprocessor && fg_CharacterInSet(c, g_Charset_Operators))
            {
              m_t << "<span class=\"highlight_operator\">";
              m_t << c;
              m_col++;
              while (*p && fg_CharacterInSet(*p, g_Charset_Operators))
              {
                m_t << *p;
                m_col++;
                ++p;
              }
              m_t << "</span>";
              break;
            }

            if (m_inCommentStart)
            {
              m_inCommentStart = false;
              if
              (
                (c == '/' && p[0] == '*' && p[1] == '*')
                || (c == '/' && p[0] == '/' && p[1] == '/')
              )
              {
                m_inDocumentationComment = true;
                m_t << "<span class=\"highlight_documentation_comment\">";
              }
            }

            if (m_inPreprocessor)
            {
              if (c == '\\')
              {
                m_t << "<span class=\"highlight_operator\">";
                m_t << c;
                m_col++;
                while (*p && fg_CharacterInSet(*p, g_Charset_Operators))
                {
                  m_t << *p;
                  m_col++;
                  ++p;
                }
                m_t << "</span>";
                break;
              }
            }

            if (fg_CharacterInSet(c, g_Charset_StartIdentifier) || (m_inPreprocessor && c == '#'))
            {
              char const *pStart = p-1;
              char const *pStartIdent = p-1;
              char const *pParse = p-1;

              char const *pClass = NULL;

              if (*pParse == '#')
              {
                ++pParse;
                while (*pParse && fg_CharacterInSet(*pParse, g_Charset_Whitespace))
                  ++pParse;
                pStartIdent = pParse;
                if (!*pParse)
                  pClass = "highlight_keyword_preprocessor_directive";
              }

              while (*pParse && fg_CharacterInSet(*pParse, g_Charset_Identifier))
                ++pParse;

              QString Identifier;
              Identifier.setLatin1(pStartIdent, pParse - pStartIdent);

              if (!m_inComment)
              {
                if (m_inPreprocessor && !pClass)
                {
                  QMap<QString, CKeywordMap *>::ConstIterator iKeyword = g_KeywordMapPreprocessorInfo.find(Identifier);
                  if (iKeyword != g_KeywordMapPreprocessorInfo.end())
                    pClass = iKeyword.data()->m_pClass;
                }
                if (!pClass)
                {
                  QMap<QString, CKeywordMap *>::ConstIterator iKeyword = g_KeywordMapInfo.find(Identifier);

                  if (iKeyword != g_KeywordMapInfo.end())
                    pClass = iKeyword.data()->m_pClass;
                }
              }

              int Length = Identifier.length();
              if (!pClass && Length >= 3)
              {
                for (int i = gc_MaxPrefixLen; i >= 0; --i)
                {
                  if (i > Length)
                    continue;

                  QString ToFind = Identifier.left(i);

                  {
                    QMap<QString, CPrefixMap *>::ConstIterator iInfo = g_PrefixMapInfo[i].find(ToFind);
                    if (iInfo != g_PrefixMapInfo[i].end())
                    {
                      if (fg_MatchOtherPrefix(Identifier, ToFind, i, Length))
                      {
                        if (i == 1 && ToFind.compare("E") == 0)
                        {
                          if (Identifier.find('_') >= 0)
                            pClass = iInfo.data()->m_pClass;
                          else
                            pClass = "highlight_enum";
                        }
                        else
                          pClass = iInfo.data()->m_pClass;

                        break;
                      }
                    }
                  }
                  {
                    QMap<QString, CPrefixMap *>::ConstIterator iInfo = g_PrefixMapInfoVar[i].find(ToFind);
                    if (iInfo != g_PrefixMapInfoVar[i].end())
                    {
                      if (fg_MatchVariablePrefix(Identifier, ToFind, i, Length))
                      {
                        pClass = iInfo.data()->m_pClass;
                        break;
                      }
                    }
                  }
                }
              }

              if (pClass)
              {
                m_t << "<span class=\"";
                m_t << pClass;
                m_t << "\">";
              }

              char const *pCopy = pStart;
              while (pCopy != pParse)
              {
                m_t << *pCopy;
                m_col++;
                ++pCopy;
              }
              p = pCopy;
              if (pClass)
                m_t << "</span>";
              break;
            }

            // Default

            p=writeUtf8Char(m_t,p-1);
            m_col++;

          }
          break;
      }
    }
  }
  
  m_deferredCodify = std::string();
}

void HtmlCodeGenerator::codify(const char *str)
{
  m_deferredCodify += str;
}

void HtmlCodeGenerator::docify(const QCString &str)
{
  handleDeferredCodify();

  //m_t << getHtmlDirEmbeddingChar(getTextDirByConfig(str));

  if (!str.isEmpty())
  {
    const char *p=str.data();
    char c;
    while (*p)
    {
      c=*p++;
      switch(c)
      {
        case '<':  m_t << "&lt;"; break;
        case '>':  m_t << "&gt;"; break;
        case '&':  m_t << "&amp;"; break;
        case '"':  m_t << "&quot;"; break;
        case '\\':
          if (*p=='<')
            { m_t << "&lt;"; p++; }
          else if (*p=='>')
            { m_t << "&gt;"; p++; }
	  else if (*p=='(')
            { m_t << "\\&zwj;("; p++; }
          else if (*p==')')
            { m_t << "\\&zwj;)"; p++; }
          else
            m_t << "\\";
          break;
        default:
          {
            uchar uc = static_cast<uchar>(c);
            if (uc<32 && !isspace(c))
            {
              m_t << "&#x24" << hex[uc>>4] << hex[uc&0xF] << ";";
            }
            else
            {
              m_t << c;
            }
          }
          break;
      }
    }
  }
}

void HtmlCodeGenerator::writeLineNumber(const QCString &ref,const QCString &filename,
                                    const QCString &anchor,int l,bool writeLineAnchor)
{
  const int maxLineNrStr = 10;
  char lineNumber[maxLineNrStr];
  char lineAnchor[maxLineNrStr];
  qsnprintf(lineNumber,maxLineNrStr,"%5d",l);
  qsnprintf(lineAnchor,maxLineNrStr,"l%05d",l);

  handleDeferredCodify();

  if (!m_lineOpen)
  {
    m_t << "<div class=\"line\">";
    m_lineOpen = TRUE;
  }

  if (writeLineAnchor) m_t << "<a id=\"" << lineAnchor << "\" name=\"" << lineAnchor << "\"></a>";
  m_t << "<span class=\"lineno\">";
  if (!filename.isEmpty())
  {
    _writeCodeLink("line",ref,filename,anchor,lineNumber,QCString());
  }
  else
  {
    docify(lineNumber);
  }
  handleDeferredCodify();

  m_t << "</span>";
  m_col=0;
}

void HtmlCodeGenerator::writeCodeLink(CodeSymbolType type,
                                      const QCString &ref,const QCString &f,
                                      const QCString &anchor, const QCString &name,
                                      const QCString &tooltip)
{
  const char *hl = codeSymbolType2Str(type);
  QCString hlClass = "code";
  if (hl)
  {
    hlClass+=" hl_";
    hlClass+=hl;
  }
  _writeCodeLink(hlClass,ref,f,anchor,name,tooltip);
}

void HtmlCodeGenerator::_writeCodeLink(const QCString &className,
                                      const QCString &ref,const QCString &f,
                                      const QCString &anchor, const QCString &name,
                                      const QCString &tooltip)
{
  handleDeferredCodify();
  if (!ref.isEmpty())
  {
    m_t << "<a class=\"" << className << "Ref\" ";
    m_t << externalLinkTarget();
  }
  else
  {
    m_t << "<a class=\"" << className << "\" ";
  }
  m_t << "href=\"";
  m_t << externalRef(m_relPath,ref,TRUE);
  if (!f.isEmpty()) m_t << addHtmlExtensionIfMissing(f);
  if (!anchor.isEmpty()) m_t << "#" << anchor;
  m_t << "\"";
  if (!tooltip.isEmpty()) m_t << " title=\"" << convertToHtml(tooltip) << "\"";
  m_t << ">";
  codify(name);
  handleDeferredCodify();
  m_t << "</a>";
  m_col+=name.length();
}

void HtmlCodeGenerator::writeTooltip(const QCString &id, const DocLinkInfo &docInfo,
                                     const QCString &decl, const QCString &desc,
                                     const SourceLinkInfo &defInfo,
                                     const SourceLinkInfo &declInfo)
{
  handleDeferredCodify();
  
  m_t << "<div class=\"ttc\" id=\"" << id << "\">";
  m_t << "<div class=\"ttname\">";
  if (!docInfo.url.isEmpty())
  {
    m_t << "<a href=\"";
    m_t << externalRef(m_relPath,docInfo.ref,TRUE);
    m_t << addHtmlExtensionIfMissing(docInfo.url);
    if (!docInfo.anchor.isEmpty())
    {
      m_t << "#" << docInfo.anchor;
    }
    m_t << "\">";
  }
  docify(docInfo.name);
  handleDeferredCodify();
  if (!docInfo.url.isEmpty())
  {
    m_t << "</a>";
  }
  m_t << "</div>";
  if (!decl.isEmpty())
  {
    m_t << "<div class=\"ttdeci\">";
    docify(decl);
    handleDeferredCodify();
    m_t << "</div>";
  }
  if (!desc.isEmpty())
  {
    m_t << "<div class=\"ttdoc\">";
    docify(desc);
    m_t << "</div>";
  }
  if (!defInfo.file.isEmpty())
  {
    m_t << "<div class=\"ttdef\"><b>Definition:</b> ";
    if (!defInfo.url.isEmpty())
    {
      m_t << "<a href=\"";
      m_t << externalRef(m_relPath,defInfo.ref,TRUE);
      m_t << addHtmlExtensionIfMissing(defInfo.url);
      if (!defInfo.anchor.isEmpty())
      {
        m_t << "#" << defInfo.anchor;
      }
      m_t << "\">";
    }
    m_t << defInfo.file << ":" << defInfo.line;
    if (!defInfo.url.isEmpty())
    {
      m_t << "</a>";
    }
    m_t << "</div>";
  }
  if (!declInfo.file.isEmpty())
  {
    m_t << "<div class=\"ttdecl\"><b>Declaration:</b> ";
    if (!declInfo.url.isEmpty())
    {
      m_t << "<a href=\"";
      m_t << externalRef(m_relPath,declInfo.ref,TRUE);
      m_t << addHtmlExtensionIfMissing(declInfo.url);
      if (!declInfo.anchor.isEmpty())
      {
        m_t << "#" << declInfo.anchor;
      }
      m_t << "\">";
    }
    m_t << declInfo.file << ":" << declInfo.line;
    if (!declInfo.url.isEmpty())
    {
      m_t << "</a>";
    }
    m_t << "</div>";
  }
  m_t << "</div>\n";
}


void HtmlCodeGenerator::startCodeLine(bool)
{
  handleDeferredCodify();
  m_col=0;
  if (!m_lineOpen)
  {
    m_t << "<div class=\"line\">";
    m_lineOpen = TRUE;
  }
}

void HtmlCodeGenerator::endCodeLine()
{
  handleDeferredCodify();
  if (m_col == 0)
  {
    m_t << " ";
    m_col++;
  }
  if (m_lineOpen)
  {
    m_t << "</div>\n";
    m_lineOpen = FALSE;
  }
}

void HtmlCodeGenerator::startFontClass(const QCString &s)
{
  handleDeferredCodify();
  m_t << "<span class=\"" << s << "\">";
  if (strcmp(s, "comment") == 0)
  {
    m_inComment = true;
    m_inCommentStart = true;
    if (m_inDocumentationComment)
      m_t << "<span class=\"highlight_documentation_comment\">";
  }
  else
  {
    m_inDocumentationComment = false;
    if (strcmp(s, "charliteral") == 0)
      m_inChar = true;
    else if (strcmp(s, "stringliteral") == 0)
      m_inString = true;
    else if (strcmp(s, "preprocessor") == 0)
      m_inPreprocessor = true;
  }
}

void HtmlCodeGenerator::endFontClass()
{
  handleDeferredCodify();
  m_t << "</span>";
  if (m_inDocumentationComment && m_inComment)
    m_t << "</span>";
  m_inComment = false;
  m_inCommentStart = false;
  m_inChar = false;
  m_inString = false;
  m_inPreprocessor = false;
}

void HtmlCodeGenerator::writeCodeAnchor(const QCString &anchor)
{
  handleDeferredCodify();
  m_t << "<a id=\"" << anchor << "\" name=\"" << anchor << "\"></a>";
}

void HtmlCodeGenerator::startCodeFragment(const QCString &)
{
  m_t << "<div class=\"fragment\">";
}

void HtmlCodeGenerator::endCodeFragment(const QCString &)
{
  //endCodeLine checks is there is still an open code line, if so closes it.
  endCodeLine();

  m_t << "</div><!-- fragment -->";
}


//--------------------------------------------------------------------------

HtmlGenerator::HtmlGenerator() : OutputGenerator(Config_getString(HTML_OUTPUT)), m_codeGen(m_t)
{
}

HtmlGenerator::HtmlGenerator(const HtmlGenerator &og) : OutputGenerator(og), m_codeGen(m_t)
{
}

HtmlGenerator &HtmlGenerator::operator=(const HtmlGenerator &og)
{
  OutputGenerator::operator=(og);
  return *this;
}

std::unique_ptr<OutputGenerator> HtmlGenerator::clone() const
{
  return std::make_unique<HtmlGenerator>(*this);
}

HtmlGenerator::~HtmlGenerator()
{
  //printf("HtmlGenerator::~HtmlGenerator()\n");
}


void HtmlGenerator::init()
{
  QCString dname = Config_getString(HTML_OUTPUT);
  Dir d(dname.str());
  if (!d.exists() && !d.mkdir(dname.str()))
  {
    term("Could not create output directory %s\n",qPrint(dname));
  }
  //writeLogo(dname);
  if (!Config_getString(HTML_HEADER).isEmpty())
  {
    g_header=fileToString(Config_getString(HTML_HEADER));
    //printf("g_header='%s'\n",qPrint(g_header));
  }
  else
  {
    g_header = ResourceMgr::instance().getAsString("header.html");
  }

  if (!Config_getString(HTML_FOOTER).isEmpty())
  {
    g_footer=fileToString(Config_getString(HTML_FOOTER));
    //printf("g_footer='%s'\n",qPrint(g_footer));
  }
  else
  {
    g_footer = ResourceMgr::instance().getAsString("footer.html");
  }

  if (Config_getBool(USE_MATHJAX))
  {
    if (!Config_getString(MATHJAX_CODEFILE).isEmpty())
    {
      g_mathjax_code=fileToString(Config_getString(MATHJAX_CODEFILE));
      //printf("g_mathjax_code='%s'\n",qPrint(g_mathjax_code));
    }
    g_latex_macro=getConvertLatexMacro();
    //printf("converted g_latex_macro='%s'\n",qPrint(g_latex_macro));
  }
  createSubDirs(d);

  ResourceMgr &mgr = ResourceMgr::instance();
  if (Config_getBool(HTML_DYNAMIC_MENUS))
  {
    mgr.copyResourceAs("tabs.css",dname,"tabs.css");
  }
  else // stylesheet for the 'old' static tabs
  {
    mgr.copyResourceAs("fixed_tabs.css",dname,"tabs.css");
  }
  mgr.copyResource("jquery.js",dname);
  if (Config_getBool(INTERACTIVE_SVG))
  {
    mgr.copyResource("svgpan.js",dname);
  }

  if (!Config_getBool(DISABLE_INDEX) && Config_getBool(HTML_DYNAMIC_MENUS))
  {
    mgr.copyResource("menu.js",dname);
  }

  {
    std::ofstream f(dname.str()+"/dynsections.js",std::ofstream::out | std::ofstream::binary);
    if (f.is_open())
    {
      TextStream t(&f);
      t << mgr.getAsString("dynsections.js");
      if (Config_getBool(SOURCE_BROWSER) && Config_getBool(SOURCE_TOOLTIPS))
      {
        t << mgr.getAsString("dynsections_tooltips.js");
      }
    }
  }
}

void HtmlGenerator::cleanup()
{
  QCString dname = Config_getString(HTML_OUTPUT);
  Dir d(dname.str());
  clearSubDirs(d);
}

/// Additional initialization after indices have been created
void HtmlGenerator::writeTabData()
{
  Doxygen::indexList->addStyleSheetFile("tabs.css");
  QCString dname=Config_getString(HTML_OUTPUT);
  ResourceMgr &mgr = ResourceMgr::instance();
  //writeColoredImgData(dname,colored_tab_data);
  mgr.copyResource("tab_a.lum",dname);
  mgr.copyResource("tab_b.lum",dname);
  mgr.copyResource("tab_h.lum",dname);
  mgr.copyResource("tab_s.lum",dname);
  mgr.copyResource("nav_h.lum",dname);
  mgr.copyResource("nav_f.lum",dname);
  mgr.copyResource("bc_s.luma",dname);
  mgr.copyResource("doxygen.svg",dname);
  Doxygen::indexList->addImageFile("doxygen.svg");
  mgr.copyResource("closed.luma",dname);
  mgr.copyResource("open.luma",dname);
  mgr.copyResource("bdwn.luma",dname);
  mgr.copyResource("sync_on.luma",dname);
  mgr.copyResource("sync_off.luma",dname);

  //{
  //  unsigned char shadow[6] = { 5, 5, 5, 5, 5, 5 };
  //  unsigned char shadow_alpha[6]  = { 80, 60, 40, 20, 10, 0 };
  //  ColoredImage img(1,6,shadow,shadow_alpha,0,0,100);
  //  img.save(dname+"/nav_g.png");
  //}
  mgr.copyResource("nav_g.png",dname);
  Doxygen::indexList->addImageFile("nav_g.png");
}

void HtmlGenerator::writeSearchData(const QCString &dname)
{
  bool serverBasedSearch = Config_getBool(SERVER_BASED_SEARCH);
  //writeImgData(dname,serverBasedSearch ? search_server_data : search_client_data);
  ResourceMgr &mgr = ResourceMgr::instance();

  mgr.copyResource("search_l.png",dname);
  Doxygen::indexList->addImageFile("search/search_l.png");
  mgr.copyResource("search_m.png",dname);
  Doxygen::indexList->addImageFile("search/search_m.png");
  mgr.copyResource("search_r.png",dname);
  Doxygen::indexList->addImageFile("search/search_r.png");
  if (serverBasedSearch)
  {
    mgr.copyResource("mag.svg",dname);
    Doxygen::indexList->addImageFile("search/mag.svg");
  }
  else
  {
    mgr.copyResource("close.svg",dname);
    Doxygen::indexList->addImageFile("search/close.svg");
    mgr.copyResource("mag_sel.svg",dname);
    Doxygen::indexList->addImageFile("search/mag_sel.svg");
  }

  QCString searchDirName = dname;
  std::ofstream f(searchDirName.str()+"/search.css",std::ofstream::out | std::ofstream::binary);
  if (f.is_open())
  {
    TextStream t(&f);
    QCString searchCss;
    if (Config_getBool(DISABLE_INDEX))
    {
      if (Config_getBool(GENERATE_TREEVIEW) && Config_getBool(FULL_SIDEBAR))
      {
        searchCss = mgr.getAsString("search_sidebar.css");
      }
      else
      {
        searchCss = mgr.getAsString("search_nomenu.css");
      }
    }
    else if (!Config_getBool(HTML_DYNAMIC_MENUS))
    {
      searchCss = mgr.getAsString("search_fixedtabs.css");
    }
    else
    {
      searchCss = mgr.getAsString("search.css");
    }
    searchCss += mgr.getAsString("search_common.css");
    searchCss = substitute(replaceColorMarkers(searchCss),"$doxygenversion",getDoxygenVersion());
    t << searchCss;
    Doxygen::indexList->addStyleSheetFile("search/search.css");
  }
}

void HtmlGenerator::writeStyleSheetFile(TextStream &t)
{
  t << replaceColorMarkers(substitute(ResourceMgr::instance().getAsString("doxygen.css"),"$doxygenversion",getDoxygenVersion()));
}

void HtmlGenerator::writeHeaderFile(TextStream &t, const QCString & /*cssname*/)
{
  t << "<!-- HTML header for doxygen " << getDoxygenVersion() << "-->\n";
  t << ResourceMgr::instance().getAsString("header.html");
}

void HtmlGenerator::writeFooterFile(TextStream &t)
{
  t << "<!-- HTML footer for doxygen " << getDoxygenVersion() << "-->\n";
  t << ResourceMgr::instance().getAsString("footer.html");
}

static std::mutex g_indexLock;

void HtmlGenerator::startFile(const QCString &name,const QCString &,
                              const QCString &title,int id)
{
  //printf("HtmlGenerator::startFile(%s)\n",qPrint(name));
  m_relPath = relativePathToRoot(name);
  QCString fileName = addHtmlExtensionIfMissing(name);
  m_lastTitle=title;

  startPlainFile(fileName);
  m_codeGen.setId(id);
  m_codeGen.setRelativePath(m_relPath);
  {
    std::lock_guard<std::mutex> lock(g_indexLock);
    Doxygen::indexList->addIndexFile(fileName);
  }

  m_lastFile = fileName;
  m_t << substituteHtmlKeywords(g_header,convertToHtml(filterTitle(title)),m_relPath);

  m_t << "<!-- " << theTranslator->trGeneratedBy() << " Doxygen "
      << getDoxygenVersion() << " -->\n";
  //bool generateTreeView = Config_getBool(GENERATE_TREEVIEW);
  bool searchEngine = Config_getBool(SEARCHENGINE);
  if (searchEngine /*&& !generateTreeView*/)
  {
    m_t << "<script type=\"text/javascript\">\n";
    m_t << "/* @license magnet:?xt=urn:btih:d3d9a9a6595521f9666a5e94cc830dab83b65699&amp;dn=expat.txt MIT */\n";
    m_t << "var searchBox = new SearchBox(\"searchBox\", \""
        << m_relPath<< "search\",'" << theTranslator->trSearch() << "','" << Doxygen::htmlFileExtension << "');\n";
    m_t << "/* @license-end */\n";
    m_t << "</script>\n";
  }
  //generateDynamicSections(t,relPath);
  m_sectionCount=0;
}

void HtmlGenerator::writeSearchInfo(TextStream &t,const QCString &)
{
  bool searchEngine      = Config_getBool(SEARCHENGINE);
  bool serverBasedSearch = Config_getBool(SERVER_BASED_SEARCH);
  if (searchEngine && !serverBasedSearch)
  {
    t << "<!-- window showing the filter options -->\n";
    t << "<div id=\"MSearchSelectWindow\"\n";
    t << "     onmouseover=\"return searchBox.OnSearchSelectShow()\"\n";
    t << "     onmouseout=\"return searchBox.OnSearchSelectHide()\"\n";
    t << "     onkeydown=\"return searchBox.OnSearchSelectKey(event)\">\n";
    t << "</div>\n";
    t << "\n";
    t << "<!-- iframe showing the search results (closed by default) -->\n";
    t << "<div id=\"MSearchResultsWindow\">\n";
    t << "<iframe src=\"javascript:void(0)\" frameborder=\"0\" \n";
    t << "        name=\"MSearchResults\" id=\"MSearchResults\">\n";
    t << "</iframe>\n";
    t << "</div>\n";
    t << "\n";
  }
}

void HtmlGenerator::writeSearchInfo()
{
  writeSearchInfo(m_t,m_relPath);
}


QCString HtmlGenerator::writeLogoAsString(const QCString &path)
{
  bool timeStamp = Config_getBool(HTML_TIMESTAMP);
  QCString result;
  if (timeStamp)
  {
    result += theTranslator->trGeneratedAt(
               dateToString(TRUE),
               Config_getString(PROJECT_NAME)
              );
  }
  else
  {
    result += theTranslator->trGeneratedBy();
  }
  result += "&#160;\n<a href=\"https://www.doxygen.org/index.html\">\n"
            "<img class=\"footer\" src=\"";
  result += path;
  result += "doxygen.svg\" width=\"104\" height=\"31\" alt=\"doxygen\"/></a> ";
  result += getDoxygenVersion();
  result += " ";
  return result;
}

void HtmlGenerator::writeLogo()
{
  m_t << writeLogoAsString(m_relPath);
}

void HtmlGenerator::writePageFooter(TextStream &t,const QCString &lastTitle,
                              const QCString &relPath,const QCString &navPath)
{
  t << substituteHtmlKeywords(g_footer,convertToHtml(lastTitle),relPath,navPath);
}

void HtmlGenerator::writeFooter(const QCString &navPath)
{
  writePageFooter(m_t,m_lastTitle,m_relPath,navPath);
}

void HtmlGenerator::endFile()
{
  endPlainFile();
}

void HtmlGenerator::startProjectNumber()
{
  m_t << "<h3 class=\"version\">";
}

void HtmlGenerator::endProjectNumber()
{
  m_t << "</h3>";
}

void HtmlGenerator::writeStyleInfo(int part)
{
  //printf("writeStyleInfo(%d)\n",part);
  if (part==0)
  {
    if (Config_getString(HTML_STYLESHEET).isEmpty()) // write default style sheet
    {
      //printf("write doxygen.css\n");
      startPlainFile("doxygen.css");

      // alternative, cooler looking titles
      //t << "H1 { text-align: center; border-width: thin none thin none;\n";
      //t << "     border-style : double; border-color : blue; padding-left : 1em; padding-right : 1em }\n";

      m_t << replaceColorMarkers(substitute(ResourceMgr::instance().getAsString("doxygen.css"),"$doxygenversion",getDoxygenVersion()));
      endPlainFile();
      Doxygen::indexList->addStyleSheetFile("doxygen.css");
    }
    else // write user defined style sheet
    {
      QCString cssname=Config_getString(HTML_STYLESHEET);
      FileInfo cssfi(cssname.str());
      if (!cssfi.exists() || !cssfi.isFile() || !cssfi.isReadable())
      {
        err("style sheet %s does not exist or is not readable!", qPrint(Config_getString(HTML_STYLESHEET)));
      }
      else
      {
        // convert style sheet to string
        QCString fileStr = fileToString(cssname);
        // write the string into the output dir
        startPlainFile(cssfi.fileName().c_str());
        m_t << fileStr;
        endPlainFile();
      }
      Doxygen::indexList->addStyleSheetFile(cssfi.fileName().c_str());
    }
    const StringVector &extraCssFiles = Config_getList(HTML_EXTRA_STYLESHEET);
    for (const auto &fileName : extraCssFiles)
    {
      if (!fileName.empty())
      {
        FileInfo fi(fileName);
        if (fi.exists())
        {
          Doxygen::indexList->addStyleSheetFile(fi.fileName().c_str());
        }
      }
    }

    Doxygen::indexList->addStyleSheetFile("jquery.js");

    Doxygen::indexList->addStyleSheetFile("dynsections.js");

    if (Config_getBool(INTERACTIVE_SVG))
    {
      Doxygen::indexList->addStyleSheetFile("svgpan.js");
    }

    if (!Config_getBool(DISABLE_INDEX) && Config_getBool(HTML_DYNAMIC_MENUS))
    {
      Doxygen::indexList->addStyleSheetFile("menu.js");
      Doxygen::indexList->addStyleSheetFile("menudata.js");
    }
  }
}

void HtmlGenerator::startDoxyAnchor(const QCString &,const QCString &,
                                    const QCString &anchor, const QCString &,
                                    const QCString &)
{
  m_t << "<a id=\"" << anchor << "\" name=\"" << anchor << "\"></a>";
}

void HtmlGenerator::endDoxyAnchor(const QCString &,const QCString &)
{
}

//void HtmlGenerator::newParagraph()
//{
//  t << "\n<p>\n";
//}

void HtmlGenerator::startParagraph(const QCString &classDef)
{
  if (!classDef.isEmpty())
    m_t << "\n<p class=\"" << classDef << "\">";
  else
    m_t << "\n<p>";
}

void HtmlGenerator::endParagraph()
{
  m_t << "</p>\n";
}

void HtmlGenerator::writeString(const QCString &text)
{
  m_t << text;
}

void HtmlGenerator::startIndexListItem()
{
  m_t << "<li>";
}

void HtmlGenerator::endIndexListItem()
{
  m_t << "</li>\n";
}

void HtmlGenerator::startIndexItem(const QCString &ref,const QCString &f)
{
  //printf("HtmlGenerator::startIndexItem(%s,%s)\n",ref,f);
  if (!ref.isEmpty() || !f.isEmpty())
  {
    if (!ref.isEmpty())
    {
      m_t << "<a class=\"elRef\" ";
      m_t << externalLinkTarget();
    }
    else
    {
      m_t << "<a class=\"el\" ";
    }
    m_t << "href=\"";
    m_t << externalRef(m_relPath,ref,TRUE);
    if (!f.isEmpty()) m_t << addHtmlExtensionIfMissing(f);
    m_t << "\">";
  }
  else
  {
    m_t << "<b>";
  }
}

void HtmlGenerator::endIndexItem(const QCString &ref,const QCString &f)
{
  //printf("HtmlGenerator::endIndexItem(%s,%s,%s)\n",ref,f,name);
  if (!ref.isEmpty() || !f.isEmpty())
  {
    m_t << "</a>";
  }
  else
  {
    m_t << "</b>";
  }
}

void HtmlGenerator::writeStartAnnoItem(const QCString &,const QCString &f,
                                       const QCString &path,const QCString &name)
{
  m_t << "<li>";
  if (!path.isEmpty()) docify(path);
  m_t << "<a class=\"el\" href=\"" << addHtmlExtensionIfMissing(f) << "\">";
  docify(name);
  m_t << "</a> ";
}

void HtmlGenerator::writeObjectLink(const QCString &ref,const QCString &f,
                                    const QCString &anchor, const QCString &name)
{
  if (!ref.isEmpty())
  {
    m_t << "<a class=\"elRef\" ";
    m_t << externalLinkTarget();
  }
  else
  {
    m_t << "<a class=\"el\" ";
  }
  m_t << "href=\"";
  m_t << externalRef(m_relPath,ref,TRUE);
  if (!f.isEmpty()) m_t << addHtmlExtensionIfMissing(f);
  if (!anchor.isEmpty()) m_t << "#" << anchor;
  m_t << "\">";
  docify(name);
  m_t << "</a>";
}

void HtmlGenerator::startTextLink(const QCString &f,const QCString &anchor)
{
  m_t << "<a href=\"";
  if (!f.isEmpty())   m_t << m_relPath << addHtmlExtensionIfMissing(f);
  if (!anchor.isEmpty()) m_t << "#" << anchor;
  m_t << "\">";
}

void HtmlGenerator::endTextLink()
{
  m_t << "</a>";
}

void HtmlGenerator::startHtmlLink(const QCString &url)
{
  bool generateTreeView = Config_getBool(GENERATE_TREEVIEW);
  m_t << "<a ";
  if (generateTreeView) m_t << "target=\"top\" ";
  m_t << "href=\"";
  if (!url.isEmpty()) m_t << url;
  m_t << "\">";
}

void HtmlGenerator::endHtmlLink()
{
  m_t << "</a>";
}

void HtmlGenerator::startGroupHeader(int extraIndentLevel)
{
  if (extraIndentLevel==2)
  {
    m_t << "<h4 class=\"groupheader\">";
  }
  else if (extraIndentLevel==1)
  {
    m_t << "<h3 class=\"groupheader\">";
  }
  else // extraIndentLevel==0
  {
    m_t << "<h2 class=\"groupheader\">";
  }
}

void HtmlGenerator::endGroupHeader(int extraIndentLevel)
{
  if (extraIndentLevel==2)
  {
    m_t << "</h4>\n";
  }
  else if (extraIndentLevel==1)
  {
    m_t << "</h3>\n";
  }
  else
  {
    m_t << "</h2>\n";
  }
}

void HtmlGenerator::startSection(const QCString &lab,const QCString &,SectionType type)
{
  switch(type)
  {
    case SectionType::Page:          m_t << "\n\n<h1>"; break;
    case SectionType::Section:       m_t << "\n\n<h2>"; break;
    case SectionType::Subsection:    m_t << "\n\n<h3>"; break;
    case SectionType::Subsubsection: m_t << "\n\n<h4>"; break;
    case SectionType::Paragraph:     m_t << "\n\n<h5>"; break;
    default: ASSERT(0); break;
  }
  m_t << "<a id=\"" << lab << "\" name=\"" << lab << "\"></a>";
}

void HtmlGenerator::endSection(const QCString &,SectionType type)
{
  switch(type)
  {
    case SectionType::Page:          m_t << "</h1>"; break;
    case SectionType::Section:       m_t << "</h2>"; break;
    case SectionType::Subsection:    m_t << "</h3>"; break;
    case SectionType::Subsubsection: m_t << "</h4>"; break;
    case SectionType::Paragraph:     m_t << "</h5>"; break;
    default: ASSERT(0); break;
  }
}

void HtmlGenerator::docify(const QCString &str)
{
  docify(str,FALSE);
}

void HtmlGenerator::docify(const QCString &str,bool inHtmlComment)
{
  if (!str.isEmpty())
  {
    const char *p=str.data();
    char c;
    while (*p)
    {
      c=*p++;
      switch(c)
      {
        case '<':  m_t << "&lt;"; break;
        case '>':  m_t << "&gt;"; break;
        case '&':  m_t << "&amp;"; break;
        case '"':  m_t << "&quot;"; break;
        case '-':  if (inHtmlComment) m_t << "&#45;"; else m_t << "-"; break;
        case '\\':
                   if (*p=='<')
                     { m_t << "&lt;"; p++; }
                   else if (*p=='>')
                     { m_t << "&gt;"; p++; }
		   else if (*p=='(')
                     { m_t << "\\&zwj;("; p++; }
                   else if (*p==')')
                     { m_t << "\\&zwj;)"; p++; }
                   else
                     m_t << "\\";
                   break;
        default:   m_t << c;
      }
    }
  }
}

void HtmlGenerator::writeChar(char c)
{
  char cs[2];
  cs[0]=c;
  cs[1]=0;
  docify(cs);
}

//--- helper function for dynamic sections -------------------------

static void startSectionHeader(TextStream &t,
                               const QCString &relPath,int sectionCount)
{
  //t << "<!-- startSectionHeader -->";
  bool dynamicSections = Config_getBool(HTML_DYNAMIC_SECTIONS);
  if (dynamicSections)
  {
    t << "<div id=\"dynsection-" << sectionCount << "\" "
         "onclick=\"return toggleVisibility(this)\" "
         "class=\"dynheader closed\" "
         "style=\"cursor:pointer;\">\n";
    t << "  <img id=\"dynsection-" << sectionCount << "-trigger\" src=\""
      << relPath << "closed.png\" alt=\"+\"/> ";
  }
  else
  {
    t << "<div class=\"dynheader\">\n";
  }
}

static void endSectionHeader(TextStream &t)
{
  //t << "<!-- endSectionHeader -->";
  t << "</div>\n";
}

static void startSectionSummary(TextStream &t,int sectionCount)
{
  //t << "<!-- startSectionSummary -->";
  bool dynamicSections = Config_getBool(HTML_DYNAMIC_SECTIONS);
  if (dynamicSections)
  {
    t << "<div id=\"dynsection-" << sectionCount << "-summary\" "
         "class=\"dynsummary\" "
         "style=\"display:block;\">\n";
  }
}

static void endSectionSummary(TextStream &t)
{
  //t << "<!-- endSectionSummary -->";
  bool dynamicSections = Config_getBool(HTML_DYNAMIC_SECTIONS);
  if (dynamicSections)
  {
    t << "</div>\n";
  }
}

static void startSectionContent(TextStream &t,int sectionCount)
{
  //t << "<!-- startSectionContent -->";
  bool dynamicSections = Config_getBool(HTML_DYNAMIC_SECTIONS);
  if (dynamicSections)
  {
    t << "<div id=\"dynsection-" << sectionCount << "-content\" "
         "class=\"dyncontent\" "
         "style=\"display:none;\">\n";
  }
  else
  {
    t << "<div class=\"dyncontent\">\n";
  }
}

static void endSectionContent(TextStream &t)
{
  //t << "<!-- endSectionContent -->";
  t << "</div>\n";
}

//----------------------------

void HtmlGenerator::startClassDiagram()
{
  startSectionHeader(m_t,m_relPath,m_sectionCount);
}

void HtmlGenerator::endClassDiagram(const ClassDiagram &d,
                                const QCString &fileName,const QCString &name)
{
  endSectionHeader(m_t);
  startSectionSummary(m_t,m_sectionCount);
  endSectionSummary(m_t);
  startSectionContent(m_t,m_sectionCount);
  TextStream tt;
  d.writeImage(tt,dir(),m_relPath,fileName);
  if (!tt.empty())
  {
    m_t << " <div class=\"center\">\n";
    m_t << "  <img src=\"";
    m_t << m_relPath << fileName << ".png\" usemap=\"#" << convertToId(name);
    m_t << "_map\" alt=\"\"/>\n";
    m_t << "  <map id=\"" << convertToId(name);
    m_t << "_map\" name=\"" << convertToId(name);
    m_t << "_map\">\n";
    m_t << tt.str();
    m_t << "  </map>\n";
    m_t << "</div>";
  }
  else
  {
    m_t << " <div class=\"center\">\n";
    m_t << "  <img src=\"";
    m_t << m_relPath << fileName << ".png\" alt=\"\"/>\n";
    m_t << " </div>";
  }
  endSectionContent(m_t);
  m_sectionCount++;
}


void HtmlGenerator::startMemberList()
{
  DBG_HTML(m_t << "<!-- startMemberList -->\n")
}

void HtmlGenerator::endMemberList()
{
  DBG_HTML(m_t << "<!-- endMemberList -->\n")
}

// anonymous type:
//  0 = single column right aligned
//  1 = double column left aligned
//  2 = single column left aligned
void HtmlGenerator::startMemberItem(const QCString &anchor,int annoType,const QCString &inheritId)
{
  DBG_HTML(m_t << "<!-- startMemberItem() -->\n")
  if (m_emptySection)
  {
    m_t << "<table class=\"memberdecls\">\n";
    m_emptySection=FALSE;
  }
  m_t << "<tr class=\"memitem:" << anchor;
  if (!inheritId.isEmpty())
  {
    m_t << " inherit " << inheritId;
  }
  m_t << "\">";
  insertMemberAlignLeft(annoType, true);
}

void HtmlGenerator::endMemberItem()
{
  m_t << "</td></tr>\n";
}

void HtmlGenerator::startMemberTemplateParams()
{
}

void HtmlGenerator::endMemberTemplateParams(const QCString &anchor,const QCString &inheritId)
{
  m_t << "</td></tr>\n";
  m_t << "<tr class=\"memitem:" << anchor;
  if (!inheritId.isEmpty())
  {
    m_t << " inherit " << inheritId;
  }
  m_t << "\"><td class=\"memTemplItemLeft\" align=\"right\" valign=\"top\">";
}

void HtmlGenerator::startCompoundTemplateParams()
{
  m_t << "<div class=\"compoundTemplParams\">";
}

void HtmlGenerator::endCompoundTemplateParams()
{
  m_t << "</div>";
}

void HtmlGenerator::insertMemberAlign(bool templ, char lastChar)
{
  DBG_HTML(m_t << "<!-- insertMemberAlign -->\n")
  QCString className = templ ? "memTemplItemRight" : "memItemRight";
  if (lastChar != '&' && lastChar != '*')
    t << "&#160;";
  m_t << "</td><td class=\"" << className << "\" valign=\"bottom\">";
}

void HtmlGenerator::insertMemberAlignLeft(int annoType, bool initTag)
{
  if (!initTag) m_t << "&#160;</td>";
  switch(annoType)
  {
    case 0:  m_t << "<td class=\"memItemLeft\" align=\"right\" valign=\"top\">"; break;
    case 1:  m_t << "<td class=\"memItemLeft\" >"; break;
    case 2:  m_t << "<td class=\"memItemLeft\" valign=\"top\">"; break;
    default: m_t << "<td class=\"memTemplParams\" colspan=\"2\">"; break;
  }
}

void HtmlGenerator::startMemberDescription(const QCString &anchor,const QCString &inheritId, bool typ)
{
  DBG_HTML(m_t << "<!-- startMemberDescription -->\n")
  if (m_emptySection)
  {
    m_t << "<table class=\"memberdecls\">\n";
    m_emptySection=FALSE;
  }
  m_t << "<tr class=\"memdesc:" << anchor;
  if (!inheritId.isEmpty())
  {
    m_t << " inherit " << inheritId;
  }
  m_t << "\">";
  m_t << "<td class=\"mdescLeft\">&#160;</td>";
  if (typ) m_t << "<td class=\"mdescLeft\">&#160;</td>";
  m_t << "<td class=\"mdescRight\">";;
}

void HtmlGenerator::endMemberDescription()
{
  DBG_HTML(m_t << "<!-- endMemberDescription -->\n")
  m_t << "<br /></td></tr>\n";
}

void HtmlGenerator::startMemberSections()
{
  DBG_HTML(m_t << "<!-- startMemberSections -->\n")
  m_emptySection=TRUE; // we postpone writing <table> until we actually
                       // write a row to prevent empty tables, which
                       // are not valid XHTML!
}

void HtmlGenerator::endMemberSections()
{
  DBG_HTML(m_t << "<!-- endMemberSections -->\n")
  if (!m_emptySection)
  {
    m_t << "</table>\n";
  }
}

void HtmlGenerator::startMemberHeader(const QCString &anchor, int typ)
{
  DBG_HTML(m_t << "<!-- startMemberHeader -->\n")
  if (!m_emptySection)
  {
    m_t << "</table>";
    m_emptySection=TRUE;
  }
  if (m_emptySection)
  {
    m_t << "<table class=\"memberdecls\">\n";
    m_emptySection=FALSE;
  }
  m_t << "<tr class=\"heading\"><td colspan=\"" << typ << "\"><h2 class=\"groupheader\">";
  if (!anchor.isEmpty())
  {
    m_t << "<a id=\"" << anchor << "\" name=\"" << anchor << "\"></a>\n";
  }
}

void HtmlGenerator::endMemberHeader()
{
  DBG_HTML(m_t << "<!-- endMemberHeader -->\n")
  m_t << "</h2></td></tr>\n";
}

void HtmlGenerator::startMemberSubtitle()
{
  DBG_HTML(m_t << "<!-- startMemberSubtitle -->\n")
  m_t << "<tr><td class=\"ititle\" colspan=\"2\">";
}

void HtmlGenerator::endMemberSubtitle()
{
  DBG_HTML(m_t << "<!-- endMemberSubtitle -->\n")
  m_t << "</td></tr>\n";
}

void HtmlGenerator::startIndexList()
{
  m_t << "<table>\n";
}

void HtmlGenerator::endIndexList()
{
  m_t << "</table>\n";
}

void HtmlGenerator::startIndexKey()
{
  // inserted 'class = ...', 02 jan 2002, jh
  m_t << "  <tr><td class=\"indexkey\">";
}

void HtmlGenerator::endIndexKey()
{
  m_t << "</td>";
}

void HtmlGenerator::startIndexValue(bool)
{
  // inserted 'class = ...', 02 jan 2002, jh
  m_t << "<td class=\"indexvalue\">";
}

void HtmlGenerator::endIndexValue(const QCString &,bool)
{
  m_t << "</td></tr>\n";
}

void HtmlGenerator::startMemberDocList()
{
  DBG_HTML(m_t << "<!-- startMemberDocList -->\n";)
}

void HtmlGenerator::endMemberDocList()
{
  DBG_HTML(m_t << "<!-- endMemberDocList -->\n";)
}

void HtmlGenerator::startMemberDoc( const QCString &clName, const QCString &memName,
                                    const QCString &anchor, const QCString &title,
                                    int memCount, int memTotal, bool showInline)
{
  DBG_HTML(m_t << "<!-- startMemberDoc -->\n";)
  m_t << "\n<h2 class=\"memtitle\">"
      << "<span class=\"permalink\"><a href=\"#" << anchor << "\">&#9670;&nbsp;</a></span>";
  docify(title);
  if (memTotal>1)
  {
    m_t << " <span class=\"overload\">[" << memCount << "/" << memTotal <<"]</span>";
  }
  m_t << "</h2>\n";
  m_t << "\n<div class=\"memitem\">\n";
  m_t << "<div class=\"memproto\">\n";
}

void HtmlGenerator::startMemberDocPrefixItem()
{
  DBG_HTML(m_t << "<!-- startMemberDocPrefixItem -->\n";)
  m_t << "<div class=\"memtemplate\">\n";
}

void HtmlGenerator::endMemberDocPrefixItem()
{
  DBG_HTML(m_t << "<!-- endMemberDocPrefixItem -->\n";)
  m_t << "</div>\n";
}

void HtmlGenerator::startMemberDocName(bool /*align*/)
{
  DBG_HTML(m_t << "<!-- startMemberDocName -->\n";)

  m_t << "      <table class=\"memname\" cellspacing=\"0\" cellpadding=\"0\">\n";

  m_t << "        <tr>\n";
  m_t << "          <td class=\"memname\">";
}

void HtmlGenerator::endMemberDocName()
{
  DBG_HTML(m_t << "<!-- endMemberDocName -->\n";)
  m_t << "</td>\n";
}

void HtmlGenerator::startParameterList(bool openBracket)
{
  DBG_HTML(m_t << "<!-- startParameterList -->\n";)
  m_t << "          <td>";
  if (openBracket) m_t << "(";
  m_t << "</td>\n";
}

void HtmlGenerator::startParameterType(bool first,const QCString &key,bool doLineBreak)
{
  if (first)
  {
    DBG_HTML(m_t << "<!-- startFirstParameterType -->\n";)
    m_t << "          <td class=\"paramtype\">";
  }
  else
  {
    DBG_HTML(m_t << "<!-- startParameterType -->\n";)
    if (doLineBreak)
      m_t << "        <tr>\n";
    if (doLineBreak || (key && *key))
      m_t << "          <td class=\"paramkey\">" << key << "</td>\n";
    if (doLineBreak)
      m_t << "          <td></td>\n";
    m_t << "          <td class=\"paramtype\">";
  }
}

void HtmlGenerator::endParameterType()
{
  DBG_HTML(m_t << "<!-- endParameterType -->\n";)
  m_t << "&#160;</td>\n";
}

void HtmlGenerator::startParameterName(bool /*oneArgOnly*/)
{
  DBG_HTML(m_t << "<!-- startParameterName -->\n";)
  m_t << "          <td class=\"paramname\">";
}

void HtmlGenerator::endParameterName(bool last,bool emptyList,bool closeBracket, bool doLineBreak)
{
  DBG_HTML(m_t << "<!-- endParameterName -->\n";)
  if (last)
  {
    if (emptyList || !doLineBreak)
    {
      if (closeBracket) m_t << "</td><td>)";
      m_t << "</td>\n";
      m_t << "          <td>";
    }
    else
    {
      m_t << "&#160;</td>\n";
      m_t << "        </tr>\n";
      m_t << "        <tr>\n";
      m_t << "          <td></td>\n";
      m_t << "          <td>";
      if (closeBracket) m_t << ")";
      m_t << "</td>\n";
      m_t << "          <td></td><td>";
    }
  }
  else
  {
    m_t << "</td>\n";
    if (doLineBreak)
      m_t << "        </tr>\n";
  }
}

void HtmlGenerator::endParameterList()
{
  DBG_HTML(m_t << "<!-- endParameterList -->\n";)
  m_t << "</td>\n";
  m_t << "        </tr>\n";
}

void HtmlGenerator::exceptionEntry(const QCString &prefix,bool closeBracket)
{
  DBG_HTML(m_t << "<!-- exceptionEntry -->\n";)
  m_t << "</td>\n";
  m_t << "        </tr>\n";
  m_t << "        <tr>\n";
  m_t << "          <td align=\"right\">";
  // colspan 2 so it gets both parameter type and parameter name columns
  if (!prefix.isEmpty())
    m_t << prefix << "</td><td>(</td><td colspan=\"2\">";
  else if (closeBracket)
    m_t << "</td><td>)</td><td></td><td>";
  else
    m_t << "</td><td></td><td colspan=\"2\">";
}

void HtmlGenerator::endMemberDoc(bool hasArgs)
{
  DBG_HTML(m_t << "<!-- endMemberDoc -->\n";)
  if (!hasArgs)
  {
    m_t << "        </tr>\n";
  }
  m_t << "      </table>\n";
 // m_t << "</div>\n";
}

void HtmlGenerator::startDotGraph()
{
  startSectionHeader(m_t,m_relPath,m_sectionCount);
}

void HtmlGenerator::endDotGraph(DotClassGraph &g)
{
  bool generateLegend = Config_getBool(GENERATE_LEGEND);
  bool umlLook = Config_getBool(UML_LOOK);
  endSectionHeader(m_t);
  startSectionSummary(m_t,m_sectionCount);
  endSectionSummary(m_t);
  startSectionContent(m_t,m_sectionCount);

  g.writeGraph(m_t,GOF_BITMAP,EOF_Html,dir(),fileName(),m_relPath,TRUE,TRUE,m_sectionCount);
  if (generateLegend && !umlLook)
  {
    m_t << "<center><span class=\"legend\">[";
    startHtmlLink((m_relPath+"graph_legend"+Doxygen::htmlFileExtension));
    m_t << theTranslator->trLegend();
    endHtmlLink();
    m_t << "]</span></center>";
  }

  endSectionContent(m_t);
  m_sectionCount++;
}

void HtmlGenerator::startInclDepGraph()
{
  startSectionHeader(m_t,m_relPath,m_sectionCount);
}

void HtmlGenerator::endInclDepGraph(DotInclDepGraph &g)
{
  endSectionHeader(m_t);
  startSectionSummary(m_t,m_sectionCount);
  endSectionSummary(m_t);
  startSectionContent(m_t,m_sectionCount);

  g.writeGraph(m_t,GOF_BITMAP,EOF_Html,dir(),fileName(),m_relPath,TRUE,m_sectionCount);

  endSectionContent(m_t);
  m_sectionCount++;
}

void HtmlGenerator::startGroupCollaboration()
{
  startSectionHeader(m_t,m_relPath,m_sectionCount);
}

void HtmlGenerator::endGroupCollaboration(DotGroupCollaboration &g)
{
  endSectionHeader(m_t);
  startSectionSummary(m_t,m_sectionCount);
  endSectionSummary(m_t);
  startSectionContent(m_t,m_sectionCount);

  g.writeGraph(m_t,GOF_BITMAP,EOF_Html,dir(),fileName(),m_relPath,TRUE,m_sectionCount);

  endSectionContent(m_t);
  m_sectionCount++;
}

void HtmlGenerator::startCallGraph()
{
  startSectionHeader(m_t,m_relPath,m_sectionCount);
}

void HtmlGenerator::endCallGraph(DotCallGraph &g)
{
  endSectionHeader(m_t);
  startSectionSummary(m_t,m_sectionCount);
  endSectionSummary(m_t);
  startSectionContent(m_t,m_sectionCount);

  g.writeGraph(m_t,GOF_BITMAP,EOF_Html,dir(),fileName(),m_relPath,TRUE,m_sectionCount);

  endSectionContent(m_t);
  m_sectionCount++;
}

void HtmlGenerator::startDirDepGraph()
{
  startSectionHeader(m_t,m_relPath,m_sectionCount);
}

void HtmlGenerator::endDirDepGraph(DotDirDeps &g)
{
  endSectionHeader(m_t);
  startSectionSummary(m_t,m_sectionCount);
  endSectionSummary(m_t);
  startSectionContent(m_t,m_sectionCount);

  g.writeGraph(m_t,GOF_BITMAP,EOF_Html,dir(),fileName(),m_relPath,TRUE,m_sectionCount);

  endSectionContent(m_t);
  m_sectionCount++;
}

void HtmlGenerator::writeGraphicalHierarchy(DotGfxHierarchyTable &g)
{
  g.writeGraph(m_t,dir(),fileName());
}

void HtmlGenerator::startMemberGroupHeader(bool)
{
  m_t << "<tr><td colspan=\"2\"><div class=\"groupHeader\">";
}

void HtmlGenerator::endMemberGroupHeader()
{
  m_t << "</div></td></tr>\n";
}

void HtmlGenerator::startMemberGroupDocs()
{
  m_t << "<tr><td colspan=\"2\"><div class=\"groupText\">";
}

void HtmlGenerator::endMemberGroupDocs()
{
  m_t << "</div></td></tr>\n";
}

void HtmlGenerator::startMemberGroup()
{
}

void HtmlGenerator::endMemberGroup(bool)
{
}

void HtmlGenerator::startIndent()
{
  DBG_HTML(m_t << "<!-- startIndent -->\n";)

  m_t << "<div class=\"memdoc\">\n";
}

void HtmlGenerator::endIndent()
{
  DBG_HTML(m_t << "<!-- endIndent -->\n";)
  m_t << "\n</div>\n" << "</div>\n";
}

void HtmlGenerator::addIndexItem(const QCString &,const QCString &)
{
}

void HtmlGenerator::writeNonBreakableSpace(int n)
{
  int i;
  for (i=0;i<n;i++)
  {
    m_t << "&#160;";
  }
}

void HtmlGenerator::startDescTable(const QCString &title)
{
  m_t << "<table class=\"fieldtable\">\n"
      << "<tr><th colspan=\"2\">" << title << "</th></tr>";
}
void HtmlGenerator::endDescTable()
{
  m_t << "</table>\n";
}

void HtmlGenerator::startDescTableRow()
{
  m_t << "<tr>";
}

void HtmlGenerator::endDescTableRow()
{
  m_t << "</tr>\n";
}

void HtmlGenerator::startDescTableTitle()
{
  m_t << "<td class=\"fieldname\">";
}

void HtmlGenerator::endDescTableTitle()
{
  m_t << "&#160;</td>";
}

void HtmlGenerator::startDescTableData()
{
  m_t << "<td class=\"fielddoc\">";
}

void HtmlGenerator::endDescTableData()
{
  m_t << "</td>";
}

void HtmlGenerator::startExamples()
{
  m_t << "<dl class=\"section examples\"><dt>";
  docify(theTranslator->trExamples());
  m_t << "</dt>";
}

void HtmlGenerator::endExamples()
{
  m_t << "</dl>\n";
}

void HtmlGenerator::startParamList(ParamListTypes,
                                const QCString &title)
{
  m_t << "<dl><dt><b>";
  docify(title);
  m_t << "</b></dt>";
}

void HtmlGenerator::endParamList()
{
  m_t << "</dl>";
}

void HtmlGenerator::writeDoc(DocNode *n,const Definition *ctx,const MemberDef *,int id)
{
  m_codeGen.setId(id);
  HtmlDocVisitor *visitor = new HtmlDocVisitor(m_t,m_codeGen,ctx);
  n->accept(visitor);
  delete visitor;
}

//---------------- helpers for index generation -----------------------------

static void startQuickIndexList(TextStream &t,bool compact,bool topLevel=TRUE)
{
  if (compact)
  {
    if (topLevel)
    {
      t << "  <div id=\"navrow1\" class=\"tabs\">\n";
    }
    else
    {
      t << "  <div id=\"navrow2\" class=\"tabs2\">\n";
    }
    t << "    <ul class=\"tablist\">\n";
  }
  else
  {
    t << "<ul>";
  }
}

static void endQuickIndexList(TextStream &t,bool compact)
{
  if (compact)
  {
    t << "    </ul>\n";
    t << "  </div>\n";
  }
  else
  {
    t << "</ul>\n";
  }
}

static void startQuickIndexItem(TextStream &t,const QCString &l,
                                bool hl,bool /*compact*/,
                                const QCString &relPath)
{
  t << "      <li";
  if (hl)
  {
    t << " class=\"current\"";
  }
  t << ">";
  if (!l.isEmpty()) t << "<a href=\"" << correctURL(l,relPath) << "\">";
  t << "<span>";
}

static void endQuickIndexItem(TextStream &t,const QCString &l)
{
  t << "</span>";
  if (!l.isEmpty()) t << "</a>";
  t << "</li>\n";
}

static bool quickLinkVisible(LayoutNavEntry::Kind kind)
{
  bool showNamespaces = Config_getBool(SHOW_NAMESPACES);
  switch (kind)
  {
    case LayoutNavEntry::MainPage:           return TRUE;
    case LayoutNavEntry::User:               return TRUE;
    case LayoutNavEntry::UserGroup:          return TRUE;
    case LayoutNavEntry::Pages:              return indexedPages>0;
    case LayoutNavEntry::Modules:            return documentedGroups>0;
    case LayoutNavEntry::Namespaces:         return documentedNamespaces>0 && showNamespaces;
    case LayoutNavEntry::NamespaceList:      return documentedNamespaces>0 && showNamespaces;
    case LayoutNavEntry::NamespaceMembers:   return documentedNamespaceMembers[NMHL_All]>0;
    case LayoutNavEntry::Concepts:           return documentedConcepts>0;
    case LayoutNavEntry::Classes:            return annotatedClasses>0;
    case LayoutNavEntry::ClassList:          return annotatedClasses>0;
    case LayoutNavEntry::ClassIndex:         return annotatedClasses>0;
    case LayoutNavEntry::ClassHierarchy:     return hierarchyClasses>0;
    case LayoutNavEntry::ClassMembers:       return documentedClassMembers[CMHL_All]>0;
    case LayoutNavEntry::Files:              return documentedFiles>0;
    case LayoutNavEntry::FileList:           return documentedFiles>0;
    case LayoutNavEntry::FileGlobals:        return documentedFileMembers[FMHL_All]>0;
    case LayoutNavEntry::Examples:           return !Doxygen::exampleLinkedMap->empty();
    case LayoutNavEntry::Interfaces:         return annotatedInterfaces>0;
    case LayoutNavEntry::InterfaceList:      return annotatedInterfaces>0;
    case LayoutNavEntry::InterfaceIndex:     return annotatedInterfaces>0;
    case LayoutNavEntry::InterfaceHierarchy: return hierarchyInterfaces>0;
    case LayoutNavEntry::Structs:            return annotatedStructs>0;
    case LayoutNavEntry::StructList:         return annotatedStructs>0;
    case LayoutNavEntry::StructIndex:        return annotatedStructs>0;
    case LayoutNavEntry::Exceptions:         return annotatedExceptions>0;
    case LayoutNavEntry::ExceptionList:      return annotatedExceptions>0;
    case LayoutNavEntry::ExceptionIndex:     return annotatedExceptions>0;
    case LayoutNavEntry::ExceptionHierarchy: return hierarchyExceptions>0;
    case LayoutNavEntry::None:             // should never happen, means not properly initialized
      assert(kind != LayoutNavEntry::None);
      return FALSE;
  }
  return FALSE;
}

static void renderQuickLinksAsTree(TextStream &t,const QCString &relPath,LayoutNavEntry *root)

{
  int count=0;
  for (const auto &entry : root->children())
  {
    if (entry->visible() && quickLinkVisible(entry->kind())) count++;
  }
  if (count>0) // at least one item is visible
  {
    startQuickIndexList(t,FALSE);
    for (const auto &entry : root->children())
    {
      if (entry->visible() && quickLinkVisible(entry->kind()))
      {
        QCString url = entry->url();
        t << "<li><a href=\"" << relPath << url << "\"><span>";
        t << fixSpaces(entry->title());
        t << "</span></a>\n";
        // recursive into child list
        renderQuickLinksAsTree(t,relPath,entry.get());
        t << "</li>";
      }
    }
    endQuickIndexList(t,FALSE);
  }
}


static void renderQuickLinksAsTabs(TextStream &t,const QCString &relPath,
                             LayoutNavEntry *hlEntry,LayoutNavEntry::Kind kind,
                             bool highlightParent,bool highlightSearch)
{
  if (hlEntry->parent()) // first draw the tabs for the parent of hlEntry
  {
    renderQuickLinksAsTabs(t,relPath,hlEntry->parent(),kind,highlightParent,highlightSearch);
  }
  if (hlEntry->parent() && !hlEntry->parent()->children().empty()) // draw tabs for row containing hlEntry
  {
    bool topLevel = hlEntry->parent()->parent()==0;
    int count=0;
    for (const auto &entry : hlEntry->parent()->children())
    {
      if (entry->visible() && quickLinkVisible(entry->kind())) count++;
    }
    if (count>0) // at least one item is visible
    {
      startQuickIndexList(t,TRUE,topLevel);
      for (const auto &entry : hlEntry->parent()->children())
      {
        if (entry->visible() && quickLinkVisible(entry->kind()))
        {
          QCString url = entry->url();
          startQuickIndexItem(t,url,
              entry.get()==hlEntry  &&
              (!entry->children().empty() ||
               (entry->kind()==kind && !highlightParent)
              ),
              TRUE,relPath);
          t << fixSpaces(entry->title());
          endQuickIndexItem(t,url);
        }
      }
      if (hlEntry->parent()==LayoutDocManager::instance().rootNavEntry()) // first row is special as it contains the search box
      {
        bool searchEngine      = Config_getBool(SEARCHENGINE);
        bool serverBasedSearch = Config_getBool(SERVER_BASED_SEARCH);
        if (searchEngine)
        {
          t << "      <li>\n";
          if (!serverBasedSearch) // pure client side search
          {
            writeClientSearchBox(t,relPath);
            t << "      </li>\n";
          }
          else // server based search
          {
            writeServerSearchBox(t,relPath,highlightSearch);
            if (!highlightSearch)
            {
              t << "      </li>\n";
            }
          }
        }
        if (!highlightSearch) // on the search page the index will be ended by the
          // page itself
        {
          endQuickIndexList(t,TRUE);
        }
      }
      else // normal case for other rows than first one
      {
        endQuickIndexList(t,TRUE);
      }
    }
  }
}

static void writeDefaultQuickLinks(TextStream &t,bool compact,
                                   HighlightedItem hli,
                                   const QCString &file,
                                   const QCString &relPath)
{
  bool serverBasedSearch = Config_getBool(SERVER_BASED_SEARCH);
  bool searchEngine = Config_getBool(SEARCHENGINE);
  bool externalSearch = Config_getBool(EXTERNAL_SEARCH);
  LayoutNavEntry *root = LayoutDocManager::instance().rootNavEntry();
  LayoutNavEntry::Kind kind = LayoutNavEntry::None;
  LayoutNavEntry::Kind altKind = LayoutNavEntry::None; // fall back for the old layout file
  bool highlightParent=FALSE;
  switch (hli) // map HLI enums to LayoutNavEntry::Kind enums
  {
    case HLI_Main:             kind = LayoutNavEntry::MainPage;         break;
    case HLI_Modules:          kind = LayoutNavEntry::Modules;          break;
    //case HLI_Directories:      kind = LayoutNavEntry::Dirs;             break;
    case HLI_Namespaces:       kind = LayoutNavEntry::NamespaceList;    altKind = LayoutNavEntry::Namespaces;  break;
    case HLI_ClassHierarchy:   kind = LayoutNavEntry::ClassHierarchy;   break;
    case HLI_InterfaceHierarchy: kind = LayoutNavEntry::InterfaceHierarchy;   break;
    case HLI_ExceptionHierarchy: kind = LayoutNavEntry::ExceptionHierarchy;   break;
    case HLI_Classes:          kind = LayoutNavEntry::ClassIndex;       altKind = LayoutNavEntry::Classes;     break;
    case HLI_Concepts:         kind = LayoutNavEntry::Concepts;         break;
    case HLI_Interfaces:       kind = LayoutNavEntry::InterfaceIndex;   altKind = LayoutNavEntry::Interfaces;  break;
    case HLI_Structs:          kind = LayoutNavEntry::StructIndex;      altKind = LayoutNavEntry::Structs;     break;
    case HLI_Exceptions:       kind = LayoutNavEntry::ExceptionIndex;   altKind = LayoutNavEntry::Exceptions;  break;
    case HLI_AnnotatedClasses: kind = LayoutNavEntry::ClassList;        altKind = LayoutNavEntry::Classes;     break;
    case HLI_AnnotatedInterfaces: kind = LayoutNavEntry::InterfaceList; altKind = LayoutNavEntry::Interfaces;  break;
    case HLI_AnnotatedStructs: kind = LayoutNavEntry::StructList;       altKind = LayoutNavEntry::Structs;     break;
    case HLI_AnnotatedExceptions: kind = LayoutNavEntry::ExceptionList; altKind = LayoutNavEntry::Exceptions;  break;
    case HLI_Files:            kind = LayoutNavEntry::FileList;         altKind = LayoutNavEntry::Files;       break;
    case HLI_NamespaceMembers: kind = LayoutNavEntry::NamespaceMembers; break;
    case HLI_Functions:        kind = LayoutNavEntry::ClassMembers;     break;
    case HLI_Globals:          kind = LayoutNavEntry::FileGlobals;      break;
    case HLI_Pages:            kind = LayoutNavEntry::Pages;            break;
    case HLI_Examples:         kind = LayoutNavEntry::Examples;         break;
    case HLI_UserGroup:        kind = LayoutNavEntry::UserGroup;        break;
    case HLI_ClassVisible:     kind = LayoutNavEntry::ClassList;        altKind = LayoutNavEntry::Classes;
                               highlightParent = TRUE; break;
    case HLI_ConceptVisible:   kind = LayoutNavEntry::Concepts;
                               highlightParent = TRUE; break;
    case HLI_InterfaceVisible: kind = LayoutNavEntry::InterfaceList;    altKind = LayoutNavEntry::Interfaces;
                               highlightParent = TRUE; break;
    case HLI_StructVisible:    kind = LayoutNavEntry::StructList;       altKind = LayoutNavEntry::Structs;
                               highlightParent = TRUE; break;
    case HLI_ExceptionVisible: kind = LayoutNavEntry::ExceptionList;    altKind = LayoutNavEntry::Exceptions;
                               highlightParent = TRUE; break;
    case HLI_NamespaceVisible: kind = LayoutNavEntry::NamespaceList;    altKind = LayoutNavEntry::Namespaces;
                               highlightParent = TRUE; break;
    case HLI_FileVisible:      kind = LayoutNavEntry::FileList;         altKind = LayoutNavEntry::Files;
                               highlightParent = TRUE; break;
    case HLI_None:   break;
    case HLI_Search: break;
  }

  if (compact && Config_getBool(HTML_DYNAMIC_MENUS))
  {
    QCString searchPage;
    if (externalSearch)
    {
      searchPage = "search" + Doxygen::htmlFileExtension;
    }
    else
    {
      searchPage = "search.php";
    }
    t << "<script type=\"text/javascript\" src=\"" << relPath << "menudata.js\"></script>\n";
    t << "<script type=\"text/javascript\" src=\"" << relPath << "menu.js\"></script>\n";
    t << "<script type=\"text/javascript\">\n";
    t << "/* @license magnet:?xt=urn:btih:d3d9a9a6595521f9666a5e94cc830dab83b65699&amp;dn=expat.txt MIT */\n";
    t << "$(function() {\n";
    t << "  initMenu('" << relPath << "',"
      << (searchEngine?"true":"false") << ","
      << (serverBasedSearch?"true":"false") << ",'"
      << searchPage << "','"
      << theTranslator->trSearch() << "');\n";
    if (Config_getBool(SEARCHENGINE))
    {
      if (!serverBasedSearch)
      {
        t << "  $(document).ready(function() { init_search(); });\n";
      }
      else
      {
        t << "  $(document).ready(function() {\n"
          << "    if ($('.searchresults').length > 0) { searchBox.DOMSearchField().focus(); }\n"
          << "  });\n";
      }
    }
    t << "});\n";
    t << "/* @license-end */\n";
    t << "</script>\n";
    t << "<div id=\"main-nav\"></div>\n";
  }
  else if (compact) // && !Config_getBool(HTML_DYNAMIC_MENUS)
  {
    // find highlighted index item
    LayoutNavEntry *hlEntry = root->find(kind,kind==LayoutNavEntry::UserGroup ? file : QCString());
    if (!hlEntry && altKind!=LayoutNavEntry::None) { hlEntry=root->find(altKind); kind=altKind; }
    if (!hlEntry) // highlighted item not found in the index! -> just show the level 1 index...
    {
      highlightParent=TRUE;
      hlEntry = root->children().front().get();
      if (hlEntry==0)
      {
        return; // argl, empty index!
      }
    }
    if (kind==LayoutNavEntry::UserGroup)
    {
      LayoutNavEntry *e = hlEntry->children().front().get();
      if (e)
      {
        hlEntry = e;
      }
    }
    renderQuickLinksAsTabs(t,relPath,hlEntry,kind,highlightParent,hli==HLI_Search);
  }
  else
  {
    renderQuickLinksAsTree(t,relPath,root);
  }
}

void HtmlGenerator::endQuickIndices()
{
  m_t << "</div><!-- top -->\n";
}

QCString HtmlGenerator::writeSplitBarAsString(const QCString &name,const QCString &relpath)
{
  bool generateTreeView = Config_getBool(GENERATE_TREEVIEW);
  QCString result;
  // write split bar
  if (generateTreeView)
  {
    if (!Config_getBool(DISABLE_INDEX) || !Config_getBool(FULL_SIDEBAR))
    {
      result += QCString(
        "<div id=\"side-nav\" class=\"ui-resizable side-nav-resizable\">\n");
    }
    result+= QCString(
     "  <div id=\"nav-tree\">\n"
     "    <div id=\"nav-tree-contents\">\n"
     "      <div id=\"nav-sync\" class=\"sync\"></div>\n"
     "    </div>\n"
     "  </div>\n"
     "  <div id=\"splitbar\" style=\"-moz-user-select:none;\" \n"
     "       class=\"ui-resizable-handle\">\n"
     "  </div>\n"
     "</div>\n"
     "<script type=\"text/javascript\">\n"
     "/* @license magnet:?xt=urn:btih:d3d9a9a6595521f9666a5e94cc830dab83b65699&amp;dn=expat.txt MIT */\n"
     "$(document).ready(function(){initNavTree('") +
     QCString(addHtmlExtensionIfMissing(name)) +
     QCString("','") + relpath +
     QCString("'); initResizable(); });\n"
     "/* @license-end */\n"
     "</script>\n"
     "<div id=\"doc-content\">\n");
  }
  return result;
}

void HtmlGenerator::writeSplitBar(const QCString &name)
{
  m_t << writeSplitBarAsString(name,m_relPath);
}

void HtmlGenerator::writeNavigationPath(const QCString &s)
{
  m_t << substitute(s,"$relpath^",m_relPath);
}

void HtmlGenerator::startContents()
{
  m_t << "<div class=\"contents\">\n";
}

void HtmlGenerator::endContents()
{
  m_t << "</div><!-- contents -->\n";
}

void HtmlGenerator::startPageDoc(const QCString &pageTitle)
{
  m_t << "<div>";
}

void HtmlGenerator::endPageDoc()
{
  m_t << "</div><!-- PageDoc -->\n";
}

void HtmlGenerator::writeQuickLinks(bool compact,HighlightedItem hli,const QCString &file)
{
  writeDefaultQuickLinks(m_t,compact,hli,file,m_relPath);
}

// PHP based search script
void HtmlGenerator::writeSearchPage()
{
  bool generateTreeView = Config_getBool(GENERATE_TREEVIEW);
  bool disableIndex = Config_getBool(DISABLE_INDEX);
  QCString projectName = Config_getString(PROJECT_NAME);
  QCString htmlOutput = Config_getString(HTML_OUTPUT);

  // OPENSEARCH_PROVIDER {
  QCString configFileName = htmlOutput+"/search_config.php";
  std::ofstream f(configFileName.str(),std::ofstream::out | std::ofstream::binary);
  if (f.is_open())
  {
    TextStream t(&f);
    t << "<?php\n\n";
    t << "$config = array(\n";
    t << "  'PROJECT_NAME' => \"" << convertToHtml(projectName) << "\",\n";
    t << "  'GENERATE_TREEVIEW' => " << (generateTreeView?"true":"false") << ",\n";
    t << "  'DISABLE_INDEX' => " << (disableIndex?"true":"false") << ",\n";
    t << ");\n\n";
    t << "$translator = array(\n";
    t << "  'search_results_title' => \"" << theTranslator->trSearchResultsTitle() << "\",\n";
    t << "  'search_results' => array(\n";
    t << "    0 => \"" << theTranslator->trSearchResults(0) << "\",\n";
    t << "    1 => \"" << theTranslator->trSearchResults(1) << "\",\n";
    t << "    2 => \"" << substitute(theTranslator->trSearchResults(2), "$", "\\$") << "\",\n";
    t << "  ),\n";
    t << "  'search_matches' => \"" << theTranslator->trSearchMatches() << "\",\n";
    t << "  'search' => \"" << theTranslator->trSearch() << "\",\n";
    t << "  'split_bar' => \"" << substitute(substitute(writeSplitBarAsString("search",""), "\"","\\\""), "\n","\\n") << "\",\n";
    t << "  'logo' => \"" << substitute(substitute(writeLogoAsString(""), "\"","\\\""), "\n","\\n") << "\",\n";
    t << ");\n\n";
    t << "?>\n";
  }
  f.close();

  ResourceMgr::instance().copyResource("search_functions.php",htmlOutput);
  ResourceMgr::instance().copyResource("search_opensearch.php",htmlOutput);
  // OPENSEARCH_PROVIDER }

  QCString fileName = htmlOutput+"/search.php";
  f.open(fileName.str(),std::ofstream::out | std::ofstream::binary);
  if (f.is_open())
  {
    TextStream t(&f);
    t << substituteHtmlKeywords(g_header,"Search","");

    t << "<!-- " << theTranslator->trGeneratedBy() << " Doxygen "
      << getDoxygenVersion() << " -->\n";
    t << "<script type=\"text/javascript\">\n";
		t << "/* @license magnet:?xt=urn:btih:d3d9a9a6595521f9666a5e94cc830dab83b65699&amp;dn=expat.txt MIT */\n";
		t << "var searchBox = new SearchBox(\"searchBox\", \""
      << "search\",'" << theTranslator->trSearch() << "','" << Doxygen::htmlFileExtension << "');\n";
		t << "/* @license-end */\n";
    t << "</script>\n";
    if (!Config_getBool(DISABLE_INDEX))
    {
      writeDefaultQuickLinks(t,TRUE,HLI_Search,QCString(),QCString());
    }
    else
    {
      t << "</div>\n";
    }

    t << "<?php\n";
    t << "require_once \"search_functions.php\";\n";
    t << "main();\n";
    t << "?>\n";

    // Write empty navigation path, to make footer connect properly
    if (generateTreeView)
    {
      t << "</div><!-- doc-content -->\n";
    }

    writePageFooter(t,"Search","","");
  }
  f.close();

  QCString scriptName = htmlOutput+"/search/search.js";
  f.open(scriptName.str(),std::ofstream::out | std::ofstream::binary);
  if (f.is_open())
  {
    TextStream t(&f);
    t << ResourceMgr::instance().getAsString("extsearch.js");
  }
  else
  {
     err("Failed to open file '%s' for writing...\n",qPrint(scriptName));
  }
}

void HtmlGenerator::writeExternalSearchPage()
{
  bool generateTreeView = Config_getBool(GENERATE_TREEVIEW);
  QCString dname = Config_getString(HTML_OUTPUT);
  QCString fileName = dname+"/search"+Doxygen::htmlFileExtension;
  std::ofstream f(fileName.str(),std::ofstream::out | std::ofstream::binary);
  if (f.is_open())
  {
    TextStream t(&f);
    t << substituteHtmlKeywords(g_header,"Search","");

    t << "<!-- " << theTranslator->trGeneratedBy() << " Doxygen "
      << getDoxygenVersion() << " -->\n";
    t << "<script type=\"text/javascript\">\n";
		t << "/* @license magnet:?xt=urn:btih:d3d9a9a6595521f9666a5e94cc830dab83b65699&amp;dn=expat.txt MIT */\n";
		t << "var searchBox = new SearchBox(\"searchBox\", \""
      << "search\",'" << theTranslator->trSearch() << "','" << Doxygen::htmlFileExtension << "');\n";
		t << "/* @license-end */\n";
    t << "</script>\n";
    if (!Config_getBool(DISABLE_INDEX))
    {
      writeDefaultQuickLinks(t,TRUE,HLI_Search,QCString(),QCString());
      t << "            <input type=\"text\" id=\"MSearchField\" name=\"query\" value=\"\" size=\"20\" accesskey=\"S\" onfocus=\"searchBox.OnSearchFieldFocus(true)\" onblur=\"searchBox.OnSearchFieldFocus(false)\"/>\n";
      t << "            </form>\n";
      t << "          </div><div class=\"right\"></div>\n";
      t << "        </div>\n";
      t << "      </li>\n";
      t << "    </ul>\n";
      t << "  </div>\n";
      t << "</div>\n";
    }
    else
    {
      t << "</div>\n";
    }
    t << writeSplitBarAsString("search","");
    t << "<div class=\"header\">\n";
    t << "  <div class=\"headertitle\">\n";
    t << "    <div class=\"title\">" << theTranslator->trSearchResultsTitle() << "</div>\n";
    t << "  </div>\n";
    t << "</div>\n";
    t << "<div class=\"contents\">\n";

    t << "<div id=\"searchresults\"></div>\n";
    t << "</div>\n";

    if (generateTreeView)
    {
      t << "</div><!-- doc-content -->\n";
    }

    writePageFooter(t,"Search","","");

  }
  f.close();

  QCString scriptName = dname+"/search/search.js";
  f.open(scriptName.str(),std::ofstream::out | std::ofstream::binary);
  if (f.is_open())
  {
    TextStream t(&f);
    t << "var searchResultsText=["
      << "\"" << theTranslator->trSearchResults(0) << "\","
      << "\"" << theTranslator->trSearchResults(1) << "\","
      << "\"" << theTranslator->trSearchResults(2) << "\"];\n";
    t << "var serverUrl=\"" << Config_getString(SEARCHENGINE_URL) << "\";\n";
    t << "var tagMap = {\n";
    bool first=TRUE;
    // add search mappings
    const StringVector &extraSearchMappings = Config_getList(EXTRA_SEARCH_MAPPINGS);
    for (const auto &ml : extraSearchMappings)
    {
      QCString mapLine = ml.c_str();
      int eqPos = mapLine.find('=');
      if (eqPos!=-1) // tag command contains a destination
      {
        QCString tagName = mapLine.left(eqPos).stripWhiteSpace();
        QCString destName = mapLine.right(mapLine.length()-eqPos-1).stripWhiteSpace();
        if (!tagName.isEmpty())
        {
          if (!first) t << ",\n";
          t << "  \"" << tagName << "\": \"" << destName << "\"";
          first=FALSE;
        }
      }
    }
    if (!first) t << "\n";
    t << "};\n\n";
    t << ResourceMgr::instance().getAsString("extsearch.js");
    t << "\n";
    t << "$(document).ready(function() {\n";
    t << "  var query = trim(getURLParameter('query'));\n";
    t << "  if (query) {\n";
    t << "    searchFor(query,0,20);\n";
    t << "  } else {\n";
    t << "    var results = $('#results');\n";
    t << "    results.html('<p>" << theTranslator->trSearchResults(0) << "</p>');\n";
    t << "  }\n";
    t << "});\n";
  }
  else
  {
     err("Failed to open file '%s' for writing...\n",qPrint(scriptName));
  }
}

void HtmlGenerator::startConstraintList(const QCString &header)
{
  m_t << "<div class=\"typeconstraint\">\n";
  m_t << "<dl><dt><b>" << header << "</b></dt><dd>\n";
  m_t << "<table border=\"0\" cellspacing=\"2\" cellpadding=\"0\">\n";
}

void HtmlGenerator::startConstraintParam()
{
  m_t << "<tr><td valign=\"top\"><em>";
}

void HtmlGenerator::endConstraintParam()
{
  m_t << "</em></td>";
}

void HtmlGenerator::startConstraintType()
{
  m_t << "<td>&#160;:</td><td valign=\"top\"><em>";
}

void HtmlGenerator::endConstraintType()
{
  m_t << "</em></td>";
}

void HtmlGenerator::startConstraintDocs()
{
  m_t << "<td>&#160;";
}

void HtmlGenerator::endConstraintDocs()
{
  m_t << "</td></tr>\n";
}

void HtmlGenerator::endConstraintList()
{
  m_t << "</table>\n";
  m_t << "</dd>\n";
  m_t << "</dl>\n";
  m_t << "</div>\n";
}

void HtmlGenerator::lineBreak(const QCString &style)
{
  if (!style.isEmpty())
  {
    m_t << "<br class=\"" << style << "\" />\n";
  }
  else
  {
    m_t << "<br />\n";
  }
}

void HtmlGenerator::startHeaderSection()
{
  m_t << "<div class=\"header\">\n";
}

void HtmlGenerator::startTitleHead(const QCString &)
{
  m_t << "  <div class=\"headertitle\">";
  startTitle();
}

void HtmlGenerator::endTitleHead(const QCString &,const QCString &)
{
  endTitle();
  m_t << "</div>\n";
}

void HtmlGenerator::endHeaderSection()
{
  m_t << "</div><!--header-->\n";
}

void HtmlGenerator::startInlineHeader()
{
  if (m_emptySection)
  {
    m_t << "<table class=\"memberdecls\">\n";
    m_emptySection=FALSE;
  }
  m_t << "<tr><td colspan=\"2\"><h3>";
}

void HtmlGenerator::endInlineHeader()
{
  m_t << "</h3></td></tr>\n";
}

void HtmlGenerator::startMemberDocSimple(bool isEnum)
{
  DBG_HTML(m_t << "<!-- startMemberDocSimple -->\n";)
  m_t << "<table class=\"fieldtable\">\n";
  m_t << "<tr><th colspan=\"" << (isEnum?"2":"3") << "\">";
  m_t << (isEnum? theTranslator->trEnumerationValues() :
       theTranslator->trCompoundMembers()) << "</th></tr>\n";
}

void HtmlGenerator::endMemberDocSimple(bool)
{
  DBG_HTML(m_t << "<!-- endMemberDocSimple -->\n";)
  m_t << "</table>\n";
}

void HtmlGenerator::startInlineMemberType()
{
  DBG_HTML(m_t << "<!-- startInlineMemberType -->\n";)
  m_t << "<tr><td class=\"fieldtype\">\n";
}

void HtmlGenerator::endInlineMemberType()
{
  DBG_HTML(m_t << "<!-- endInlineMemberType -->\n";)
  m_t << "</td>\n";
}

void HtmlGenerator::startInlineMemberName()
{
  DBG_HTML(m_t << "<!-- startInlineMemberName -->\n";)
  m_t << "<td class=\"fieldname\">\n";
}

void HtmlGenerator::endInlineMemberName()
{
  DBG_HTML(m_t << "<!-- endInlineMemberName -->\n";)
  m_t << "</td>\n";
}

void HtmlGenerator::startInlineMemberDoc()
{
  DBG_HTML(m_t << "<!-- startInlineMemberDoc -->\n";)
  m_t << "<td class=\"fielddoc\">\n";
}

void HtmlGenerator::endInlineMemberDoc()
{
  DBG_HTML(m_t << "<!-- endInlineMemberDoc -->\n";)
  m_t << "</td></tr>\n";
}

void HtmlGenerator::startLabels()
{
  DBG_HTML(m_t << "<!-- startLabels -->\n";)
  m_t << "<span class=\"mlabels\">";
}

void HtmlGenerator::writeLabel(const QCString &l,bool /*isLast*/)
{
  DBG_HTML(m_t << "<!-- writeLabel(" << l << ") -->\n";)
  //m_t << "<tt>[" << l << "]</tt>";
  //if (!isLast) m_t << ", ";
  m_t << "<span class=\"mlabel\">" << l << "</span>";
}

void HtmlGenerator::endLabels()
{
  DBG_HTML(m_t << "<!-- endLabels -->\n";)
  m_t << "</span>";
}

void HtmlGenerator::writeInheritedSectionTitle(
                  const QCString &id,    const QCString &ref,
                  const QCString &file,  const QCString &anchor,
                  const QCString &title, const QCString &name)
{
  DBG_HTML(m_t << "<!-- writeInheritedSectionTitle -->\n";)
  QCString a = anchor;
  if (!a.isEmpty()) a.prepend("#");
  QCString classLink = QCString("<a class=\"el\" ");
  if (!ref.isEmpty())
  {
    classLink+= externalLinkTarget();
    classLink += " href=\"";
    classLink+= externalRef(m_relPath,ref,TRUE);
  }
  else
  {
    classLink += "href=\"";
    classLink+=m_relPath;
  }
  classLink=classLink+addHtmlExtensionIfMissing(file)+a;
  classLink+=QCString("\">")+convertToHtml(name,FALSE)+"</a>";
  m_t << "<tr class=\"inherit_header " << id << "\">"
    << "<td colspan=\"2\" onclick=\"javascript:toggleInherit('" << id << "')\">"
    << "<img src=\"" << m_relPath << "closed.png\" alt=\"-\"/>&#160;"
    << theTranslator->trInheritedFrom(convertToHtml(title,FALSE),classLink)
    << "</td></tr>\n";
}

void HtmlGenerator::writeSummaryLink(const QCString &file,const QCString &anchor,const QCString &title,bool first)
{
  if (first)
  {
    m_t << "  <div class=\"summary\">\n";
  }
  else
  {
    m_t << " &#124;\n";
  }
  m_t << "<a href=\"";
  if (!file.isEmpty())
  {
    m_t << m_relPath << addHtmlExtensionIfMissing(file);
  }
  else if (!anchor.isEmpty())
  {
    m_t << "#";
    m_t << anchor;
  }
  m_t << "\">";
  m_t << title;
  m_t << "</a>";
}

void HtmlGenerator::endMemberDeclaration(const QCString &anchor,const QCString &inheritId)
{
  m_t << "<tr class=\"separator:" << anchor;
  if (!inheritId.isEmpty())
  {
    m_t << " inherit " << inheritId;
  }
  m_t << "\"><td class=\"memSeparator\" colspan=\"2\">&#160;</td></tr>\n";
}

QCString HtmlGenerator::getMathJaxMacros()
{
  return getConvertLatexMacro();
}
