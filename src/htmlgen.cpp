/******************************************************************************
 *
 *
 *
 * Copyright (C) 1997-2015 by Dimitri van Heesch.
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
#include <qdir.h>
#include <qmap.h>
#include <qregexp.h>
#include "message.h"
#include "htmlgen.h"
#include "config.h"
#include "util.h"
#include "doxygen.h"
#include "logos.h"
#include "diagram.h"
#include "version.h"
#include "dot.h"
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


//#define DBG_HTML(x) x;
#define DBG_HTML(x)

static QCString g_header;
static QCString g_footer;
static QCString g_mathjax_code;


// note: this is only active if DISABLE_INDEX=YES, if DISABLE_INDEX is disabled, this
// part will be rendered inside menu.js
static void writeClientSearchBox(FTextStream &t,const char *relPath)
{
  t << "        <div id=\"MSearchBox\" class=\"MSearchBoxInactive\">\n";
  t << "        <span class=\"left\">\n";
  t << "          <img id=\"MSearchSelect\" src=\"" << relPath << "search/mag_sel.png\"\n";
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
    << "<img id=\"MSearchCloseImg\" border=\"0\" src=\"" << relPath << "search/close.png\" alt=\"\"/></a>\n";
  t << "          </span>\n";
  t << "        </div>\n";
}

// note: this is only active if DISABLE_INDEX=YES. if DISABLE_INDEX is disabled, this
// part will be rendered inside menu.js
static void writeServerSearchBox(FTextStream &t,const char *relPath,bool highlightSearch)
{
  static bool externalSearch = Config_getBool(EXTERNAL_SEARCH);
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
  t << "              <img id=\"MSearchSelect\" src=\"" << relPath << "search/mag.png\" alt=\"\"/>\n";
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

/// Clear a text block \a s from \a begin to \a end markers
QCString clearBlock(const char *s,const char *begin,const char *end)
{
  if (s==0 || begin==0 || end==0) return s;
  const char *p, *q;
  int beginLen = qstrlen(begin);
  int endLen = qstrlen(end);
  int resLen = 0;
  for (p=s; (q=strstr(p,begin))!=0; p=q+endLen)
  {
    resLen+=(int)(q-p);
    p=q+beginLen;
    if ((q=strstr(p,end))==0)
    {
      resLen+=beginLen;
      break;
    }
  }
  resLen+=qstrlen(p);
  // resLen is the length of the string without the marked block

  QCString result(resLen+1);
  char *r;
  for (r=result.rawData(), p=s; (q=strstr(p,begin))!=0; p=q+endLen)
  {
    int l = (int)(q-p);
    memcpy(r,p,l);
    r+=l;
    p=q+beginLen;
    if ((q=strstr(p,end))==0)
    {
      memcpy(r,begin,beginLen);
      r+=beginLen;
      break;
    }
  }
  qstrcpy(r,p);
  return result;
}
//----------------------------------------------------------------------

QCString selectBlock(const QCString& s,const QCString &name,bool enable)
{
  // TODO: this is an expensive function that is called a lot -> optimize it
  const QCString begin = "<!--BEGIN " + name + "-->";
  const QCString end = "<!--END " + name + "-->";
  const QCString nobegin = "<!--BEGIN !" + name + "-->";
  const QCString noend = "<!--END !" + name + "-->";

  QCString result = s;
  if (enable)
  {
    result = substitute(result, begin, "");
    result = substitute(result, end, "");
    result = clearBlock(result, nobegin, noend);
  }
  else
  {
    result = substitute(result, nobegin, "");
    result = substitute(result, noend, "");
    result = clearBlock(result, begin, end);
  }

  return result;
}

static QCString getSearchBox(bool serverSide, QCString relPath, bool highlightSearch)
{
  QGString result;
  FTextStream t(&result);
  if (serverSide)
  {
    writeServerSearchBox(t, relPath, highlightSearch);
  }
  else
  {
    writeClientSearchBox(t, relPath);
  }
  return QCString(result);
}

static QCString removeEmptyLines(const QCString &s)
{
  BufStr out(s.length()+1);
  const char *p=s.data();
  if (p)
  {
    char c;
    while ((c=*p++))
    {
      if (c=='\n')
      {
        const char *e = p;
        while (*e==' ' || *e=='\t') e++;
        if (*e=='\n')
        {
          p=e;
        }
        else out.addChar(c);
      }
      else
      {
        out.addChar(c);
      }
    }
  }
  out.addChar('\0');
  //printf("removeEmptyLines(%s)=%s\n",s.data(),out.data());
  return out.data();
}

static QCString substituteHtmlKeywords(const QCString &s,
                                       const QCString &title,
                                       const QCString &relPath,
                                       const QCString &navPath=QCString())
{
  // Build CSS/Javascript tags depending on treeview, search engine settings
  QCString cssFile;
  QStrList extraCssFile;
  QCString generatedBy;
  QCString treeViewCssJs;
  QCString searchCssJs;
  QCString searchBox;
  QCString mathJaxJs;
  QCString extraCssText;

  static QCString projectName = Config_getString(PROJECT_NAME);
  static bool timeStamp = Config_getBool(HTML_TIMESTAMP);
  static bool treeView = Config_getBool(GENERATE_TREEVIEW);
  static bool searchEngine = Config_getBool(SEARCHENGINE);
  static bool serverBasedSearch = Config_getBool(SERVER_BASED_SEARCH);
  static bool mathJax = Config_getBool(USE_MATHJAX);
  static QCString mathJaxFormat = Config_getEnum(MATHJAX_FORMAT);
  static bool disableIndex = Config_getBool(DISABLE_INDEX);
  static bool hasProjectName = !projectName.isEmpty();
  static bool hasProjectNumber = !Config_getString(PROJECT_NUMBER).isEmpty();
  static bool hasProjectBrief = !Config_getString(PROJECT_BRIEF).isEmpty();
  static bool hasProjectLogo = !Config_getString(PROJECT_LOGO).isEmpty();
  static bool titleArea = (hasProjectName || hasProjectBrief || hasProjectLogo || (disableIndex && searchEngine));

  cssFile = Config_getString(HTML_STYLESHEET);
  if (cssFile.isEmpty())
  {
    cssFile = "doxygen.css";
  }
  else
  {
    QFileInfo cssfi(cssFile);
    if (cssfi.exists())
    {
      cssFile = cssfi.fileName().utf8();
    }
    else
    {
      cssFile = "doxygen.css";
    }
  }

  extraCssText = "";
  extraCssFile = Config_getList(HTML_EXTRA_STYLESHEET);
  for (uint i=0; i<extraCssFile.count(); ++i)
  {
    QCString fileName(extraCssFile.at(i));
    if (!fileName.isEmpty())
    {
      QFileInfo fi(fileName);
      if (fi.exists())
      {
        extraCssText += "<link href=\"$relpath^"+stripPath(fileName)+"\" rel=\"stylesheet\" type=\"text/css\"/>\n";
      }
    }
  }

  if (timeStamp)
  {
    generatedBy = theTranslator->trGeneratedAt(dateToString(TRUE), convertToHtml(Config_getString(PROJECT_NAME)));
  }
  else
  {
    generatedBy = theTranslator->trGeneratedBy();
  }

  if (treeView)
  {
    treeViewCssJs = "<link href=\"$relpath^navtree.css\" rel=\"stylesheet\" type=\"text/css\"/>\n"
			"<script type=\"text/javascript\" src=\"$relpath^resize.js\"></script>\n"
			"<script type=\"text/javascript\" src=\"$relpath^navtreedata.js\"></script>\n"
			"<script type=\"text/javascript\" src=\"$relpath^navtree.js\"></script>\n"
			"<script type=\"text/javascript\">\n"
			"/* @license magnet:?xt=urn:btih:cf05388f2679ee054f2beb29a391d25f4e673ac3&amp;dn=gpl-2.0.txt GPL-v2 */\n"
			"  $(document).ready(initResizable);\n"
			"/* @license-end */"
			"</script>";
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
					"/* @license magnet:?xt=urn:btih:cf05388f2679ee054f2beb29a391d25f4e673ac3&amp;dn=gpl-2.0.txt GPL-v2 */\n"
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
					"/* @license magnet:?xt=urn:btih:cf05388f2679ee054f2beb29a391d25f4e673ac3&amp;dn=gpl-2.0.txt GPL-v2 */\n"
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
    QCString path = Config_getString(MATHJAX_RELPATH);
    if (path.isEmpty() || path.left(2)=="..") // relative path
    {
      path.prepend(relPath);
    }
    mathJaxJs = "<script type=\"text/x-mathjax-config\">\n"
                "  MathJax.Hub.Config({\n"
                "    extensions: [\"tex2jax.js\"";
    QStrList &mathJaxExtensions = Config_getList(MATHJAX_EXTENSIONS);
    const char *s = mathJaxExtensions.first();
    while (s)
    {
      mathJaxJs+= ", \""+QCString(s)+".js\"";
      s = mathJaxExtensions.next();
    }
    if (mathJaxFormat.isEmpty())
    {
      mathJaxFormat = "HTML-CSS";
    }
    mathJaxJs += "],\n"
                 "    jax: [\"input/TeX\",\"output/"+mathJaxFormat+"\"],\n"
                 "});\n";
    if (!g_mathjax_code.isEmpty())
    {
      mathJaxJs += g_mathjax_code;
      mathJaxJs += "\n";
    }
    mathJaxJs += "</script>";
    mathJaxJs += "<script type=\"text/javascript\" async=\"async\" src=\"" + path + "MathJax.js\"></script>\n";
  }

  // first substitute generic keywords
  QCString result = substituteKeywords(s,title,
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
  result = selectBlock(result,"DISABLE_INDEX",disableIndex);
  result = selectBlock(result,"GENERATE_TREEVIEW",treeView);
  result = selectBlock(result,"SEARCHENGINE",searchEngine);
  result = selectBlock(result,"TITLEAREA",titleArea);
  result = selectBlock(result,"PROJECT_NAME",hasProjectName);
  result = selectBlock(result,"PROJECT_NUMBER",hasProjectNumber);
  result = selectBlock(result,"PROJECT_BRIEF",hasProjectBrief);
  result = selectBlock(result,"PROJECT_LOGO",hasProjectLogo);

  result = removeEmptyLines(result);

  return result;
}

//--------------------------------------------------------------------------

HtmlCodeGenerator::HtmlCodeGenerator()
   : m_streamSet(FALSE), m_col(0), m_inComment(false), m_inChar(false), m_inString(false), m_inPreprocessor(false), m_inCommentStart(false), m_inDocumentationComment(false)
{
}

HtmlCodeGenerator::HtmlCodeGenerator(FTextStream &t,const QCString &relPath)
   : m_col(0), m_inComment(false), m_inChar(false), m_inString(false), m_inPreprocessor(false), m_inCommentStart(false), m_inDocumentationComment(false), m_relPath(relPath)
{
  setTextStream(t);
}

void HtmlCodeGenerator::setTextStream(FTextStream &t)
{
  m_streamSet = t.device()!=0;
  m_t.setDevice(t.device());
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

    // Preprocessor directive
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
  static int tabSize = Config_getInt(TAB_SIZE);
  if (str && m_streamSet)
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
                   else
                     m_t << "\\";
                   m_col++;
                   break;
        case ' ': m_t << " ";m_col++;
                   break;
        default:
    
          if (m_inChar || m_inString)
          {
            p=writeUtf8Char(m_t,p-1);    
            m_col++;
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
                        pClass = iInfo.data()->m_pClass;
                        /*if (NodeType == NodeType_IdentifierType)
                          pColor = pEnum;
                        else
                          pColor = pEnumerator;*/
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

void HtmlCodeGenerator::docify(const char *str)
{
  handleDeferredCodify();

  m_t << getHtmlDirEmbedingChar(getTextDirByConfig(str));

  if (str && m_streamSet)
  {
    const char *p=str;
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
                   else
                     m_t << "\\";
                   break;
        default:   m_t << c;
      }
    }
  }
}

void HtmlCodeGenerator::writeLineNumber(const char *ref,const char *filename,
                                    const char *anchor,int l)
{
  if (!m_streamSet) return;
  const int maxLineNrStr = 10;
  char lineNumber[maxLineNrStr];
  char lineAnchor[maxLineNrStr];
  qsnprintf(lineNumber,maxLineNrStr,"%5d",l);
  qsnprintf(lineAnchor,maxLineNrStr,"l%05d",l);

  handleDeferredCodify();
  
  m_t << "<div class=\"line\">";
  m_t << "<a name=\"" << lineAnchor << "\"></a><span class=\"lineno\">";
  if (filename)
  {
    _writeCodeLink("line",ref,filename,anchor,lineNumber,0);
  }
  else
  {
    docify(lineNumber);
  }
  handleDeferredCodify();

  m_t << "</span>";
  m_t << "&#160;";
}

void HtmlCodeGenerator::writeCodeLink(const char *ref,const char *f,
                                      const char *anchor, const char *name,
                                      const char *tooltip)
{
  if (!m_streamSet) return;
  //printf("writeCodeLink(ref=%s,f=%s,anchor=%s,name=%s,tooltip=%s)\n",ref,f,anchor,name,tooltip);
  _writeCodeLink("code",ref,f,anchor,name,tooltip);
}

void HtmlCodeGenerator::_writeCodeLink(const char *className,
                                      const char *ref,const char *f,
                                      const char *anchor, const char *name,
                                      const char *tooltip)
{
  handleDeferredCodify();
  if (ref)
  {
    m_t << "<a class=\"" << className << "Ref\" ";
    m_t << externalLinkTarget() << externalRef(m_relPath,ref,FALSE);
  }
  else
  {
    m_t << "<a class=\"" << className << "\" ";
  }
  m_t << "href=\"";
  m_t << externalRef(m_relPath,ref,TRUE);
  if (f) m_t << f << Doxygen::htmlFileExtension;
  if (anchor) m_t << "#" << anchor;
  m_t << "\"";
  if (tooltip) m_t << " title=\"" << convertToHtml(tooltip) << "\"";
  m_t << ">";
  codify(name);
  handleDeferredCodify();
  m_t << "</a>";
  m_col+=qstrlen(name);
}

void HtmlCodeGenerator::writeTooltip(const char *id, const DocLinkInfo &docInfo,
                                     const char *decl, const char *desc,
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
    m_t << docInfo.url << Doxygen::htmlFileExtension;
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
  if (decl)
  {
    m_t << "<div class=\"ttdeci\">";
    docify(decl);
    handleDeferredCodify();
    m_t << "</div>";
  }
  if (desc)
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
      m_t << defInfo.url << Doxygen::htmlFileExtension;
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
      m_t << declInfo.url << Doxygen::htmlFileExtension;
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
  m_t << "</div>" << endl;
}


void HtmlCodeGenerator::startCodeLine(bool hasLineNumbers)
{ 
  handleDeferredCodify();
  if (m_streamSet)
  {
    if (!hasLineNumbers) m_t << "<div class=\"line\">";
    m_col=0;
  }
}

void HtmlCodeGenerator::endCodeLine()
{
  handleDeferredCodify();
  if (m_streamSet) m_t << "</div>";
}

void HtmlCodeGenerator::startFontClass(const char *s)
{ 
  handleDeferredCodify();
  if (m_streamSet) 
  {
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
}

void HtmlCodeGenerator::endFontClass()
{ 
  handleDeferredCodify();
  if (m_streamSet) 
  {
    m_t << "</span>";
    if (m_inDocumentationComment && m_inComment)
      m_t << "</span>"; 
  }
  m_inComment = false;
  m_inCommentStart = false;
  m_inChar = false;
  m_inString = false;
  m_inPreprocessor = false;
}

void HtmlCodeGenerator::writeCodeAnchor(const char *anchor)
{
  handleDeferredCodify();
  if (m_streamSet) m_t << "<a name=\"" << anchor << "\"></a>";
}

//--------------------------------------------------------------------------

HtmlGenerator::HtmlGenerator() : OutputGenerator()
{
  dir=Config_getString(HTML_OUTPUT);
  m_emptySection=FALSE;
  m_sectionCount=0;
}

HtmlGenerator::~HtmlGenerator()
{
  //printf("HtmlGenerator::~HtmlGenerator()\n");
}

void HtmlGenerator::init()
{
  QCString dname=Config_getString(HTML_OUTPUT);
  QDir d(dname);
  if (!d.exists() && !d.mkdir(dname))
  {
    err("Could not create output directory %s\n",dname.data());
    exit(1);
  }
  //writeLogo(dname);
  if (!Config_getString(HTML_HEADER).isEmpty())
  {
    g_header=fileToString(Config_getString(HTML_HEADER));
    //printf("g_header='%s'\n",g_header.data());
  }
  else
  {
    g_header = ResourceMgr::instance().getAsString("header.html");
  }

  if (!Config_getString(HTML_FOOTER).isEmpty())
  {
    g_footer=fileToString(Config_getString(HTML_FOOTER));
    //printf("g_footer='%s'\n",g_footer.data());
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
      //printf("g_mathjax_code='%s'\n",g_mathjax_code.data());
    }
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
    QFile f(dname+"/dynsections.js");
    if (f.open(IO_WriteOnly))
    {
      FTextStream t(&f);
      t << mgr.getAsString("dynsections.js");
      if (Config_getBool(SOURCE_BROWSER) && Config_getBool(SOURCE_TOOLTIPS))
      {
        t << endl <<
          "$(document).ready(function() {\n"
          "  $('.code,.codeRef').each(function() {\n"
          "    $(this).data('powertip',$('#'+$(this).attr('href').replace(/.*\\//,'').replace(/[^a-z_A-Z0-9]/g,'_')).html());\n"
          "    $(this).powerTip({ placement: 's', smartPlacement: true, mouseOnToPopup: true });\n"
          "  });\n"
          "});\n";
      }
    }
  }
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
  mgr.copyResource("doxygen.luma",dname);
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
}

void HtmlGenerator::writeSearchData(const char *dir)
{
  static bool serverBasedSearch = Config_getBool(SERVER_BASED_SEARCH);
  //writeImgData(dir,serverBasedSearch ? search_server_data : search_client_data);
  ResourceMgr &mgr = ResourceMgr::instance();

  mgr.copyResource("search_l.png",dir);
  Doxygen::indexList->addImageFile("search/search_l.png");
  mgr.copyResource("search_m.png",dir);
  Doxygen::indexList->addImageFile("search/search_m.png");
  mgr.copyResource("search_r.png",dir);
  Doxygen::indexList->addImageFile("search/search_r.png");
  if (serverBasedSearch)
  {
    mgr.copyResource("mag.png",dir);
    Doxygen::indexList->addImageFile("search/mag.png");
  }
  else
  {
    mgr.copyResource("close.png",dir);
    Doxygen::indexList->addImageFile("search/close.png");
    mgr.copyResource("mag_sel.png",dir);
    Doxygen::indexList->addImageFile("search/mag_sel.png");
  }

  QCString searchDirName = Config_getString(HTML_OUTPUT)+"/search";
  QFile f(searchDirName+"/search.css");
  if (f.open(IO_WriteOnly))
  {
    FTextStream t(&f);
    QCString searchCss;
    if (Config_getBool(DISABLE_INDEX))
    {
      searchCss = mgr.getAsString("search_nomenu.css");
    }
    else if (!Config_getBool(HTML_DYNAMIC_MENUS))
    {
      searchCss = mgr.getAsString("search_fixedtabs.css");
    }
    else
    {
      searchCss = mgr.getAsString("search.css");
    }
    searchCss = substitute(replaceColorMarkers(searchCss),"$doxygenversion",versionString);
    t << searchCss;
    Doxygen::indexList->addStyleSheetFile("search/search.css");
  }
}

void HtmlGenerator::writeStyleSheetFile(QFile &file)
{
  FTextStream t(&file);
  t << replaceColorMarkers(substitute(ResourceMgr::instance().getAsString("doxygen.css"),"$doxygenversion",versionString));
}

void HtmlGenerator::writeHeaderFile(QFile &file, const char * /*cssname*/)
{
  FTextStream t(&file);
  t << "<!-- HTML header for doxygen " << versionString << "-->" << endl;
  t << ResourceMgr::instance().getAsString("header.html");
}

void HtmlGenerator::writeFooterFile(QFile &file)
{
  FTextStream t(&file);
  t << "<!-- HTML footer for doxygen " << versionString << "-->" <<  endl;
  t << ResourceMgr::instance().getAsString("footer.html");
}

void HtmlGenerator::startFile(const char *name,const char *,
                              const char *title)
{
  //printf("HtmlGenerator::startFile(%s)\n",name);
  QCString fileName=name;
  lastTitle=title;
  relPath = relativePathToRoot(fileName);

  if (fileName.right(Doxygen::htmlFileExtension.length())!=Doxygen::htmlFileExtension)
  {
    fileName+=Doxygen::htmlFileExtension;
  }
  startPlainFile(fileName);
  m_codeGen.setTextStream(t);
  m_codeGen.setRelativePath(relPath);
  Doxygen::indexList->addIndexFile(fileName);

  lastFile = fileName;
  t << substituteHtmlKeywords(g_header,convertToHtml(filterTitle(title)),relPath);

  t << "<!-- " << theTranslator->trGeneratedBy() << " Doxygen "
    << versionString << " -->" << endl;
  //static bool generateTreeView = Config_getBool(GENERATE_TREEVIEW);
  static bool searchEngine = Config_getBool(SEARCHENGINE);
  if (searchEngine /*&& !generateTreeView*/)
  {
    t << "<script type=\"text/javascript\">\n";
		t << "/* @license magnet:?xt=urn:btih:cf05388f2679ee054f2beb29a391d25f4e673ac3&amp;dn=gpl-2.0.txt GPL-v2 */\n";
		t << "var searchBox = new SearchBox(\"searchBox\", \""
      << relPath<< "search\",false,'" << theTranslator->trSearch() << "');\n";
		t << "/* @license-end */\n";
    t << "</script>\n";
  }
  //generateDynamicSections(t,relPath);
  m_sectionCount=0;
}

void HtmlGenerator::writeSearchInfo(FTextStream &t,const QCString &relPath)
{
  static bool searchEngine      = Config_getBool(SEARCHENGINE);
  static bool serverBasedSearch = Config_getBool(SERVER_BASED_SEARCH);
  if (searchEngine && !serverBasedSearch)
  {
    (void)relPath;
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
  writeSearchInfo(t,relPath);
}


QCString HtmlGenerator::writeLogoAsString(const char *path)
{
  static bool timeStamp = Config_getBool(HTML_TIMESTAMP);
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
  result += "&#160;\n<a href=\"http://www.doxygen.org/index.html\">\n"
            "<img class=\"footer\" src=\"";
  result += path;
  result += "doxygen.png\" alt=\"doxygen\"/></a> ";
  result += versionString;
  result += " ";
  return result;
}

void HtmlGenerator::writeLogo()
{
  t << writeLogoAsString(relPath);
}

void HtmlGenerator::writePageFooter(FTextStream &t,const QCString &lastTitle,
                              const QCString &relPath,const QCString &navPath)
{
  t << substituteHtmlKeywords(g_footer,convertToHtml(lastTitle),relPath,navPath);
}

void HtmlGenerator::writeFooter(const char *navPath)
{
  writePageFooter(t,lastTitle,relPath,navPath);
}

void HtmlGenerator::endFile()
{
  endPlainFile();
}

void HtmlGenerator::startProjectNumber()
{
  t << "<h3 class=\"version\">";
}

void HtmlGenerator::endProjectNumber()
{
  t << "</h3>";
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
      //t << "H1 { text-align: center; border-width: thin none thin none;" << endl;
      //t << "     border-style : double; border-color : blue; padding-left : 1em; padding-right : 1em }" << endl;

      t << replaceColorMarkers(substitute(ResourceMgr::instance().getAsString("doxygen.css"),"$doxygenversion",versionString));
      endPlainFile();
      Doxygen::indexList->addStyleSheetFile("doxygen.css");
    }
    else // write user defined style sheet
    {
      QCString cssname=Config_getString(HTML_STYLESHEET);
      QFileInfo cssfi(cssname);
      if (!cssfi.exists() || !cssfi.isFile() || !cssfi.isReadable())
      {
        err("style sheet %s does not exist or is not readable!", Config_getString(HTML_STYLESHEET).data());
      }
      else
      {
        // convert style sheet to string
        QCString fileStr = fileToString(cssname);
        // write the string into the output dir
        startPlainFile(cssfi.fileName().utf8());
        t << fileStr;
        endPlainFile();
      }
      Doxygen::indexList->addStyleSheetFile(cssfi.fileName().utf8());
    }
    static QStrList extraCssFile = Config_getList(HTML_EXTRA_STYLESHEET);
    for (uint i=0; i<extraCssFile.count(); ++i)
    {
      QCString fileName(extraCssFile.at(i));
      if (!fileName.isEmpty())
      {
        QFileInfo fi(fileName);
        if (fi.exists())
        {
          Doxygen::indexList->addStyleSheetFile(fi.fileName().utf8());
        }
      }
    }

    Doxygen::indexList->addStyleSheetFile("jquery.js");
    Doxygen::indexList->addStyleSheetFile("dynsections.js");
    if (Config_getBool(INTERACTIVE_SVG))
    {
      Doxygen::indexList->addStyleSheetFile("svgpan.js");
    }
  }
}

void HtmlGenerator::startDoxyAnchor(const char *,const char *,
                                    const char *anchor, const char *,
                                    const char *)
{
  t << "<a id=\"" << anchor << "\"></a>";
}

void HtmlGenerator::endDoxyAnchor(const char *,const char *)
{
}

//void HtmlGenerator::newParagraph()
//{
//  t << endl << "<p>" << endl;
//}

void HtmlGenerator::startParagraph(const char *classDef)
{
  if (classDef)
    t << endl << "<p class=\"" << classDef << "\">";
  else
    t << endl << "<p>";
}

void HtmlGenerator::endParagraph()
{
  t << "</p>" << endl;
}

void HtmlGenerator::writeString(const char *text)
{
  t << text;
}

void HtmlGenerator::startIndexListItem()
{
  t << "<li>";
}

void HtmlGenerator::endIndexListItem()
{
  t << "</li>" << endl;
}

void HtmlGenerator::startIndexItem(const char *ref,const char *f)
{
  //printf("HtmlGenerator::startIndexItem(%s,%s)\n",ref,f);
  if (ref || f)
  {
    if (ref)
    {
      t << "<a class=\"elRef\" ";
      t << externalLinkTarget() << externalRef(relPath,ref,FALSE);
    }
    else
    {
      t << "<a class=\"el\" ";
    }
    t << "href=\"";
    t << externalRef(relPath,ref,TRUE);
    if (f) t << f << Doxygen::htmlFileExtension;
    t << "\">";
  }
  else
  {
    t << "<b>";
  }
}

void HtmlGenerator::endIndexItem(const char *ref,const char *f)
{
  //printf("HtmlGenerator::endIndexItem(%s,%s,%s)\n",ref,f,name);
  if (ref || f)
  {
    t << "</a>";
  }
  else
  {
    t << "</b>";
  }
}

void HtmlGenerator::writeStartAnnoItem(const char *,const char *f,
                                       const char *path,const char *name)
{
  t << "<li>";
  if (path) docify(path);
  t << "<a class=\"el\" href=\"" << f << Doxygen::htmlFileExtension << "\">";
  docify(name);
  t << "</a> ";
}

void HtmlGenerator::writeObjectLink(const char *ref,const char *f,
                                    const char *anchor, const char *name)
{
  if (ref)
  {
    t << "<a class=\"elRef\" ";
    t << externalLinkTarget() << externalRef(relPath,ref,FALSE);
  }
  else
  {
    t << "<a class=\"el\" ";
  }
  t << "href=\"";
  t << externalRef(relPath,ref,TRUE);
  if (f) t << f << Doxygen::htmlFileExtension;
  if (anchor) t << "#" << anchor;
  t << "\">";
  docify(name);
  t << "</a>";
}

void HtmlGenerator::startTextLink(const char *f,const char *anchor)
{
  t << "<a href=\"";
  if (f)   t << relPath << f << Doxygen::htmlFileExtension;
  if (anchor) t << "#" << anchor;
  t << "\">";
}

void HtmlGenerator::endTextLink()
{
  t << "</a>";
}

void HtmlGenerator::startHtmlLink(const char *url)
{
  static bool generateTreeView = Config_getBool(GENERATE_TREEVIEW);
  t << "<a ";
  if (generateTreeView) t << "target=\"top\" ";
  t << "href=\"";
  if (url) t << url;
  t << "\">";
}

void HtmlGenerator::endHtmlLink()
{
  t << "</a>";
}

void HtmlGenerator::startGroupHeader(int extraIndentLevel)
{
  if (extraIndentLevel==2)
  {
    t << "<h4 class=\"groupheader\">";
  }
  else if (extraIndentLevel==1)
  {
    t << "<h3 class=\"groupheader\">";
  }
  else // extraIndentLevel==0
  {
    t << "<h2 class=\"groupheader\">";
  }
}

void HtmlGenerator::endGroupHeader(int extraIndentLevel)
{
  if (extraIndentLevel==2)
  {
    t << "</h4>" << endl;
  }
  else if (extraIndentLevel==1)
  {
    t << "</h3>" << endl;
  }
  else
  {
    t << "</h2>" << endl;
  }
}

void HtmlGenerator::startSection(const char *lab,const char *,SectionInfo::SectionType type)
{
  switch(type)
  {
    case SectionInfo::Page:          t << "\n\n<h1>"; break;
    case SectionInfo::Section:       t << "\n\n<h2>"; break;
    case SectionInfo::Subsection:    t << "\n\n<h3>"; break;
    case SectionInfo::Subsubsection: t << "\n\n<h4>"; break;
    case SectionInfo::Paragraph:     t << "\n\n<h5>"; break;
    default: ASSERT(0); break;
  }
  t << "<a id=\"" << lab << "\"></a>";
}

void HtmlGenerator::endSection(const char *,SectionInfo::SectionType type)
{
  switch(type)
  {
    case SectionInfo::Page:          t << "</h1>"; break;
    case SectionInfo::Section:       t << "</h2>"; break;
    case SectionInfo::Subsection:    t << "</h3>"; break;
    case SectionInfo::Subsubsection: t << "</h4>"; break;
    case SectionInfo::Paragraph:     t << "</h5>"; break;
    default: ASSERT(0); break;
  }
}

void HtmlGenerator::docify(const char *str)
{
  docify(str,FALSE);
}

void HtmlGenerator::docify(const char *str,bool inHtmlComment)
{
  if (str)
  {
    const char *p=str;
    char c;
    while (*p)
    {
      c=*p++;
      switch(c)
      {
        case '<':  t << "&lt;"; break;
        case '>':  t << "&gt;"; break;
        case '&':  t << "&amp;"; break;
        case '"':  t << "&quot;"; break;
        case '-':  if (inHtmlComment) t << "&#45;"; else t << "-"; break;
        case '\\':
                   if (*p=='<')
                     { t << "&lt;"; p++; }
                   else if (*p=='>')
                     { t << "&gt;"; p++; }
                   else
                     t << "\\";
                   break;
        default:   t << c;
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

static void startSectionHeader(FTextStream &t,
                               const QCString &relPath,int sectionCount)
{
  //t << "<!-- startSectionHeader -->";
  static bool dynamicSections = Config_getBool(HTML_DYNAMIC_SECTIONS);
  if (dynamicSections)
  {
    t << "<div id=\"dynsection-" << sectionCount << "\" "
         "onclick=\"return toggleVisibility(this)\" "
         "class=\"dynheader closed\" "
         "style=\"cursor:pointer;\">" << endl;
    t << "  <img id=\"dynsection-" << sectionCount << "-trigger\" src=\""
      << relPath << "closed.png\" alt=\"+\"/> ";
  }
  else
  {
    t << "<div class=\"dynheader\">" << endl;
  }
}

static void endSectionHeader(FTextStream &t)
{
  //t << "<!-- endSectionHeader -->";
  t << "</div>" << endl;
}

static void startSectionSummary(FTextStream &t,int sectionCount)
{
  //t << "<!-- startSectionSummary -->";
  static bool dynamicSections = Config_getBool(HTML_DYNAMIC_SECTIONS);
  if (dynamicSections)
  {
    t << "<div id=\"dynsection-" << sectionCount << "-summary\" "
         "class=\"dynsummary\" "
         "style=\"display:block;\">" << endl;
  }
}

static void endSectionSummary(FTextStream &t)
{
  //t << "<!-- endSectionSummary -->";
  static bool dynamicSections = Config_getBool(HTML_DYNAMIC_SECTIONS);
  if (dynamicSections)
  {
    t << "</div>" << endl;
  }
}

static void startSectionContent(FTextStream &t,int sectionCount)
{
  //t << "<!-- startSectionContent -->";
  static bool dynamicSections = Config_getBool(HTML_DYNAMIC_SECTIONS);
  if (dynamicSections)
  {
    t << "<div id=\"dynsection-" << sectionCount << "-content\" "
         "class=\"dyncontent\" "
         "style=\"display:none;\">" << endl;
  }
  else
  {
    t << "<div class=\"dyncontent\">" << endl;
  }
}

static void endSectionContent(FTextStream &t)
{
  //t << "<!-- endSectionContent -->";
  t << "</div>" << endl;
}

//----------------------------

void HtmlGenerator::startClassDiagram()
{
  startSectionHeader(t,relPath,m_sectionCount);
}

void HtmlGenerator::endClassDiagram(const ClassDiagram &d,
                                const char *fileName,const char *name)
{
  QGString result;
  FTextStream tt(&result);

  endSectionHeader(t);
  startSectionSummary(t,m_sectionCount);
  endSectionSummary(t);
  startSectionContent(t,m_sectionCount);
  d.writeImage(tt,dir,relPath,fileName);
  if (!result.isEmpty())
  {
    t << " <div class=\"center\">" << endl;
    t << "  <img src=\"";
    t << relPath << fileName << ".png\" usemap=\"#" << convertToId(name);
    t << "_map\" alt=\"\"/>" << endl;
    t << "  <map id=\"" << convertToId(name);
    t << "_map\" name=\"" << convertToId(name);
    t << "_map\">" << endl;
    t << result;
    t << "  </map>" << endl;
    t << "</div>";
  }
  else
  {
    t << " <div class=\"center\">" << endl;
    t << "  <img src=\"";
    t << relPath << fileName << ".png\" alt=\"\"/>" << endl;
    t << " </div>";
  }
  endSectionContent(t);
  m_sectionCount++;
}


void HtmlGenerator::startMemberList()
{
  DBG_HTML(t << "<!-- startMemberList -->" << endl)
}

void HtmlGenerator::endMemberList()
{
  DBG_HTML(t << "<!-- endMemberList -->" << endl)
}

// anonymous type:
//  0 = single column right aligned
//  1 = double column left aligned
//  2 = single column left aligned
void HtmlGenerator::startMemberItem(const char *anchor,int annoType,const char *inheritId)
{
  DBG_HTML(t << "<!-- startMemberItem() -->" << endl)
  if (m_emptySection)
  {
    t << "<table class=\"memberdecls\">" << endl;
    m_emptySection=FALSE;
  }
  t << "<tr class=\"memitem:" << anchor;
  if (inheritId)
  {
    t << " inherit " << inheritId;
  }
  t << "\">";
  insertMemberAlignLeft(annoType, true);
}

void HtmlGenerator::endMemberItem()
{
  t << "</td></tr>";
  t << endl;
}

void HtmlGenerator::startMemberTemplateParams()
{
}

void HtmlGenerator::endMemberTemplateParams(const char *anchor,const char *inheritId)
{
  t << "</td></tr>" << endl;
  t << "<tr class=\"memitem:" << anchor;
  if (inheritId)
  {
    t << " inherit " << inheritId;
  }
  t << "\"><td class=\"memTemplItemLeft\" align=\"right\" valign=\"top\">";
}


void HtmlGenerator::insertMemberAlign(bool templ, char lastChar)
{ 
  DBG_HTML(t << "<!-- insertMemberAlign -->" << endl)
  QCString className = templ ? "memTemplItemRight" : "memItemRight";
  if (lastChar != '&' && lastChar != '*')
    t << "&#160;";
  t << "</td><td class=\"" << className << "\" valign=\"bottom\">";
}

void HtmlGenerator::insertMemberAlignLeft(int annoType, bool initTag)
{
  if (!initTag) t << "&#160;</td>";
  switch(annoType)
  {
    case 0:  t << "<td class=\"memItemLeft\" align=\"right\" valign=\"top\">"; break;
    case 1:  t << "<td class=\"memItemLeft\" >"; break;
    case 2:  t << "<td class=\"memItemLeft\" valign=\"top\">"; break;
    default: t << "<td class=\"memTemplParams\" colspan=\"2\">"; break;
  }
}

void HtmlGenerator::startMemberDescription(const char *anchor,const char *inheritId, bool typ)
{
  DBG_HTML(t << "<!-- startMemberDescription -->" << endl)
    if (m_emptySection)
    {
      t << "<table class=\"memberdecls\">" << endl;
      m_emptySection=FALSE;
    }
  t << "<tr class=\"memdesc:" << anchor;
  if (inheritId)
  {
    t << " inherit " << inheritId;
  }
  t << "\">";
  t << "<td class=\"mdescLeft\">&#160;</td>";
  if (typ) t << "<td class=\"mdescLeft\">&#160;</td>";
  t << "<td class=\"mdescRight\">";;
}

void HtmlGenerator::endMemberDescription()
{
  DBG_HTML(t << "<!-- endMemberDescription -->" << endl)
  t << "<br /></td></tr>" << endl;
}

void HtmlGenerator::startMemberSections()
{
  DBG_HTML(t << "<!-- startMemberSections -->" << endl)
  m_emptySection=TRUE; // we postpone writing <table> until we actually
                       // write a row to prevent empty tables, which
                       // are not valid XHTML!
}

void HtmlGenerator::endMemberSections()
{
  DBG_HTML(t << "<!-- endMemberSections -->" << endl)
  if (!m_emptySection)
  {
    t << "</table>" << endl;
  }
}

void HtmlGenerator::startMemberHeader(const char *anchor, int typ)
{
  DBG_HTML(t << "<!-- startMemberHeader -->" << endl)
  if (!m_emptySection)
  {
    t << "</table>";
    m_emptySection=TRUE;
  }
  if (m_emptySection)
  {
    t << "<table class=\"memberdecls\">" << endl;
    m_emptySection=FALSE;
  }
  t << "<tr class=\"heading\"><td colspan=\"" << typ << "\"><h2 class=\"groupheader\">";
  if (anchor)
  {
    t << "<a name=\"" << anchor << "\"></a>" << endl;
  }
}

void HtmlGenerator::endMemberHeader()
{
  DBG_HTML(t << "<!-- endMemberHeader -->" << endl)
  t << "</h2></td></tr>" << endl;
}

void HtmlGenerator::startMemberSubtitle()
{
  DBG_HTML(t << "<!-- startMemberSubtitle -->" << endl)
  t << "<tr><td class=\"ititle\" colspan=\"2\">";
}

void HtmlGenerator::endMemberSubtitle()
{
  DBG_HTML(t << "<!-- endMemberSubtitle -->" << endl)
  t << "</td></tr>" << endl;
}

void HtmlGenerator::startIndexList()
{
  t << "<table>"  << endl;
}

void HtmlGenerator::endIndexList()
{
  t << "</table>" << endl;
}

void HtmlGenerator::startIndexKey()
{
  // inserted 'class = ...', 02 jan 2002, jh
  t << "  <tr><td class=\"indexkey\">";
}

void HtmlGenerator::endIndexKey()
{
  t << "</td>";
}

void HtmlGenerator::startIndexValue(bool)
{
  // inserted 'class = ...', 02 jan 2002, jh
  t << "<td class=\"indexvalue\">";
}

void HtmlGenerator::endIndexValue(const char *,bool)
{
  t << "</td></tr>" << endl;
}

void HtmlGenerator::startMemberDocList()
{
  DBG_HTML(t << "<!-- startMemberDocList -->" << endl;)
}

void HtmlGenerator::endMemberDocList()
{
  DBG_HTML(t << "<!-- endMemberDocList -->" << endl;)
}

void HtmlGenerator::startMemberDoc( const char *clName, const char *memName,
                                    const char *anchor, const char *title,
                                    int memCount, int memTotal, bool showInline)
{
  DBG_HTML(t << "<!-- startMemberDoc -->" << endl;)
  t << "\n<h2 class=\"memtitle\">"
    << "<span class=\"permalink\"><a href=\"#" << anchor << "\">&#9670;&nbsp;</a></span>";
  docify(title);
  if (memTotal>1)
  {
    t << " <span class=\"overload\">[" << memCount << "/" << memTotal <<"]</span>";
  }
  t << "</h2>"
    << endl;
  t << "\n<div class=\"memitem\">" << endl;
  t << "<div class=\"memproto\">" << endl;
}

void HtmlGenerator::startMemberDocPrefixItem()
{
  DBG_HTML(t << "<!-- startMemberDocPrefixItem -->" << endl;)
  t << "<div class=\"memtemplate\">" << endl;
}

void HtmlGenerator::endMemberDocPrefixItem()
{
  DBG_HTML(t << "<!-- endMemberDocPrefixItem -->" << endl;)
  t << "</div>" << endl;
}

void HtmlGenerator::startMemberDocName(bool /*align*/)
{
  DBG_HTML(t << "<!-- startMemberDocName -->" << endl;)

  t << "      <table class=\"memname\" cellspacing=\"0\" cellpadding=\"0\">" << endl;

  t << "        <tr>" << endl;
  t << "          <td class=\"memname\">";
}

void HtmlGenerator::endMemberDocName()
{
  DBG_HTML(t << "<!-- endMemberDocName -->" << endl;)
  t << "</td>" << endl;
}

void HtmlGenerator::startParameterList(bool openBracket)
{
  DBG_HTML(t << "<!-- startParameterList -->" << endl;)
  t << "          <td>";
  if (openBracket) t << "(";
  t << "</td>" << endl;
}

void HtmlGenerator::startParameterType(bool first,const char *key,bool doLineBreak)
{
  if (first)
  {
    DBG_HTML(t << "<!-- startFirstParameterType -->" << endl;)
    t << "          <td class=\"paramtype\">";
  }
  else
  {
    DBG_HTML(t << "<!-- startParameterType -->" << endl;)
    if (doLineBreak)
      t << "        <tr>" << endl;
    if (doLineBreak || (key && *key))
    {
      t << "          <td class=\"paramkey\">";
      if (key && *key) t << key;
      t << "</td>" << endl;
    }
    if (doLineBreak)
        t << "          <td></td>" << endl;
    t << "          <td class=\"paramtype\">";
  }
}

void HtmlGenerator::endParameterType()
{
  DBG_HTML(t << "<!-- endParameterType -->" << endl;)
  t << "&#160;</td>" << endl;
}

void HtmlGenerator::startParameterName(bool /*oneArgOnly*/)
{
  DBG_HTML(t << "<!-- startParameterName -->" << endl;)
  t << "          <td class=\"paramname\">";
}

void HtmlGenerator::endParameterName(bool last,bool emptyList,bool closeBracket, bool doLineBreak)
{
  DBG_HTML(t << "<!-- endParameterName -->" << endl;)
  if (last)
  {
    if (emptyList || !doLineBreak)
    {
      if (closeBracket) t << "</td><td>)";
      t << "</td>" << endl;
      t << "          <td>";
    }
    else
    {
      t << "&#160;</td>" << endl;
      t << "        </tr>" << endl;
      t << "        <tr>" << endl;
      t << "          <td></td>" << endl;
      t << "          <td>";
      if (closeBracket) t << ")";
      t << "</td>" << endl;
      t << "          <td></td><td>";
    }
  }
  else
  {
    t << "</td>" << endl;
    if (doLineBreak)
      t << "        </tr>" << endl;
  }
}

void HtmlGenerator::endParameterList()
{
  DBG_HTML(t << "<!-- endParameterList -->" << endl;)
  t << "</td>" << endl;
  t << "        </tr>" << endl;
}

void HtmlGenerator::exceptionEntry(const char* prefix,bool closeBracket)
{
  DBG_HTML(t << "<!-- exceptionEntry -->" << endl;)
  t << "</td>" << endl;
  t << "        </tr>" << endl;
  t << "        <tr>" << endl;
  t << "          <td align=\"right\">";
  // colspan 2 so it gets both parameter type and parameter name columns
  if (prefix)
    t << prefix << "</td><td>(</td><td colspan=\"2\">";
  else if (closeBracket)
    t << "</td><td>)</td><td></td><td>";
  else
    t << "</td><td></td><td colspan=\"2\">";
}

void HtmlGenerator::endMemberDoc(bool hasArgs)
{
  DBG_HTML(t << "<!-- endMemberDoc -->" << endl;)
  if (!hasArgs)
  {
    t << "        </tr>" << endl;
  }
  t << "      </table>" << endl;
 // t << "</div>" << endl;
}

void HtmlGenerator::startDotGraph()
{
  startSectionHeader(t,relPath,m_sectionCount);
}

void HtmlGenerator::endDotGraph(const DotClassGraph &g)
{
  bool generateLegend = Config_getBool(GENERATE_LEGEND);
  bool umlLook = Config_getBool(UML_LOOK);
  endSectionHeader(t);
  startSectionSummary(t,m_sectionCount);
  endSectionSummary(t);
  startSectionContent(t,m_sectionCount);

  g.writeGraph(t,GOF_BITMAP,EOF_Html,dir,fileName,relPath,TRUE,TRUE,m_sectionCount);
  if (generateLegend && !umlLook)
  {
    t << "<center><span class=\"legend\">[";
    startHtmlLink(relPath+"graph_legend"+Doxygen::htmlFileExtension);
    t << theTranslator->trLegend();
    endHtmlLink();
    t << "]</span></center>";
  }

  endSectionContent(t);
  m_sectionCount++;
}

void HtmlGenerator::startInclDepGraph()
{
  startSectionHeader(t,relPath,m_sectionCount);
}

void HtmlGenerator::endInclDepGraph(const DotInclDepGraph &g)
{
  endSectionHeader(t);
  startSectionSummary(t,m_sectionCount);
  endSectionSummary(t);
  startSectionContent(t,m_sectionCount);

  g.writeGraph(t,GOF_BITMAP,EOF_Html,dir,fileName,relPath,TRUE,m_sectionCount);

  endSectionContent(t);
  m_sectionCount++;
}

void HtmlGenerator::startGroupCollaboration()
{
  startSectionHeader(t,relPath,m_sectionCount);
}

void HtmlGenerator::endGroupCollaboration(const DotGroupCollaboration &g)
{
  endSectionHeader(t);
  startSectionSummary(t,m_sectionCount);
  endSectionSummary(t);
  startSectionContent(t,m_sectionCount);

  g.writeGraph(t,GOF_BITMAP,EOF_Html,dir,fileName,relPath,TRUE,m_sectionCount);

  endSectionContent(t);
  m_sectionCount++;
}

void HtmlGenerator::startCallGraph()
{
  startSectionHeader(t,relPath,m_sectionCount);
}

void HtmlGenerator::endCallGraph(const DotCallGraph &g)
{
  endSectionHeader(t);
  startSectionSummary(t,m_sectionCount);
  endSectionSummary(t);
  startSectionContent(t,m_sectionCount);

  g.writeGraph(t,GOF_BITMAP,EOF_Html,dir,fileName,relPath,TRUE,m_sectionCount);

  endSectionContent(t);
  m_sectionCount++;
}

void HtmlGenerator::startDirDepGraph()
{
  startSectionHeader(t,relPath,m_sectionCount);
}

void HtmlGenerator::endDirDepGraph(const DotDirDeps &g)
{
  endSectionHeader(t);
  startSectionSummary(t,m_sectionCount);
  endSectionSummary(t);
  startSectionContent(t,m_sectionCount);

  g.writeGraph(t,GOF_BITMAP,EOF_Html,dir,fileName,relPath,TRUE,m_sectionCount);

  endSectionContent(t);
  m_sectionCount++;
}

void HtmlGenerator::writeGraphicalHierarchy(const DotGfxHierarchyTable &g)
{
  g.writeGraph(t,dir,fileName);
}

void HtmlGenerator::startMemberGroupHeader(bool)
{
  t << "<tr><td colspan=\"2\"><div class=\"groupHeader\">";
}

void HtmlGenerator::endMemberGroupHeader()
{
  t << "</div></td></tr>" << endl;
}

void HtmlGenerator::startMemberGroupDocs()
{
  t << "<tr><td colspan=\"2\"><div class=\"groupText\">";
}

void HtmlGenerator::endMemberGroupDocs()
{
  t << "</div></td></tr>" << endl;
}

void HtmlGenerator::startMemberGroup()
{
}

void HtmlGenerator::endMemberGroup(bool)
{
}

void HtmlGenerator::startIndent()
{
  DBG_HTML(t << "<!-- startIndent -->" << endl;)

  t << "<div class=\"memdoc\">\n";
}

void HtmlGenerator::endIndent()
{
  DBG_HTML(t << "<!-- endIndent -->" << endl;)
  t << endl << "</div>" << endl << "</div>" << endl;
}

void HtmlGenerator::addIndexItem(const char *,const char *)
{
}

void HtmlGenerator::writeNonBreakableSpace(int n)
{
  int i;
  for (i=0;i<n;i++)
  {
    t << "&#160;";
  }
}

void HtmlGenerator::startDescTable(const char *title)
{
  t << "<table class=\"fieldtable\">" << endl
    << "<tr><th colspan=\"2\">" << title << "</th></tr>";
}
void HtmlGenerator::endDescTable()
{
  t << "</table>" << endl;
}

void HtmlGenerator::startDescTableRow()
{
  t << "<tr>";
}

void HtmlGenerator::endDescTableRow()
{
  t << "</tr>" << endl;
}

void HtmlGenerator::startDescTableTitle()
{
  t << "<td class=\"fieldname\">";
}

void HtmlGenerator::endDescTableTitle()
{
  t << "&#160;</td>";
}

void HtmlGenerator::startDescTableData()
{
  t << "<td class=\"fielddoc\">";
}

void HtmlGenerator::endDescTableData()
{
  t << "</td>";
}

void HtmlGenerator::startExamples()
{
  t << "<dl class=\"section examples\"><dt>";
  docify(theTranslator->trExamples());
  t << "</dt>";
}

void HtmlGenerator::endExamples()
{
  t << "</dl>" << endl;
}

void HtmlGenerator::startParamList(ParamListTypes,
                                const char *title)
{
  t << "<dl><dt><b>";
  docify(title);
  t << "</b></dt>";
}

void HtmlGenerator::endParamList()
{
  t << "</dl>";
}

void HtmlGenerator::writeDoc(DocNode *n,Definition *ctx,MemberDef *)
{
  HtmlDocVisitor *visitor = new HtmlDocVisitor(t,m_codeGen,ctx);
  n->accept(visitor);
  delete visitor;
}

//---------------- helpers for index generation -----------------------------

static void startQuickIndexList(FTextStream &t,bool compact,bool topLevel=TRUE)
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

static void endQuickIndexList(FTextStream &t,bool compact)
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

static void startQuickIndexItem(FTextStream &t,const char *l,
                                bool hl,bool /*compact*/,
                                const QCString &relPath)
{
  t << "      <li";
  if (hl)
  {
    t << " class=\"current\"";
  }
  t << ">";
  if (l) t << "<a href=\"" << correctURL(l,relPath) << "\">";
  t << "<span>";
}

static void endQuickIndexItem(FTextStream &t,const char *l)
{
  t << "</span>";
  if (l) t << "</a>";
  t << "</li>\n";
}

static bool quickLinkVisible(LayoutNavEntry::Kind kind)
{
  static bool showFiles = Config_getBool(SHOW_FILES);
  static bool showNamespaces = Config_getBool(SHOW_NAMESPACES);
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
    case LayoutNavEntry::Classes:            return annotatedClasses>0;
    case LayoutNavEntry::ClassList:          return annotatedClasses>0;
    case LayoutNavEntry::ClassIndex:         return annotatedClasses>0;
    case LayoutNavEntry::ClassHierarchy:     return hierarchyClasses>0;
    case LayoutNavEntry::ClassMembers:       return documentedClassMembers[CMHL_All]>0;
    case LayoutNavEntry::Files:              return documentedHtmlFiles>0 && showFiles;
    case LayoutNavEntry::FileList:           return documentedHtmlFiles>0 && showFiles;
    case LayoutNavEntry::FileGlobals:        return documentedFileMembers[FMHL_All]>0;
    case LayoutNavEntry::Examples:           return Doxygen::exampleSDict->count()>0;
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

static void renderQuickLinksAsTree(FTextStream &t,const QCString &relPath,LayoutNavEntry *root)

{
  QListIterator<LayoutNavEntry> li(root->children());
  LayoutNavEntry *entry;
  int count=0;
  for (li.toFirst();(entry=li.current());++li)
  {
    if (entry->visible() && quickLinkVisible(entry->kind())) count++;
  }
  if (count>0) // at least one item is visible
  {
    startQuickIndexList(t,FALSE);
    for (li.toFirst();(entry=li.current());++li)
    {
      if (entry->visible() && quickLinkVisible(entry->kind()))
      {
        QCString url = entry->url();
        t << "<li><a href=\"" << relPath << url << "\"><span>";
        t << fixSpaces(entry->title());
        t << "</span></a>\n";
        // recursive into child list
        renderQuickLinksAsTree(t,relPath,entry);
        t << "</li>";
      }
    }
    endQuickIndexList(t,FALSE);
  }
}


static void renderQuickLinksAsTabs(FTextStream &t,const QCString &relPath,
                             LayoutNavEntry *hlEntry,LayoutNavEntry::Kind kind,
                             bool highlightParent,bool highlightSearch)
{
  if (hlEntry->parent()) // first draw the tabs for the parent of hlEntry
  {
    renderQuickLinksAsTabs(t,relPath,hlEntry->parent(),kind,highlightParent,highlightSearch);
  }
  if (hlEntry->parent() && hlEntry->parent()->children().count()>0) // draw tabs for row containing hlEntry
  {
    bool topLevel = hlEntry->parent()->parent()==0;
    QListIterator<LayoutNavEntry> li(hlEntry->parent()->children());
    LayoutNavEntry *entry;

    int count=0;
    for (li.toFirst();(entry=li.current());++li)
    {
      if (entry->visible() && quickLinkVisible(entry->kind())) count++;
    }
    if (count>0) // at least one item is visible
    {
      startQuickIndexList(t,TRUE,topLevel);
      for (li.toFirst();(entry=li.current());++li)
      {
        if (entry->visible() && quickLinkVisible(entry->kind()))
        {
          QCString url = entry->url();
          startQuickIndexItem(t,url,
              entry==hlEntry  &&
              (entry->children().count()>0 ||
               (entry->kind()==kind && !highlightParent)
              ),
              TRUE,relPath);
          t << fixSpaces(entry->title());
          endQuickIndexItem(t,url);
        }
      }
      if (hlEntry->parent()==LayoutDocManager::instance().rootNavEntry()) // first row is special as it contains the search box
      {
        static bool searchEngine      = Config_getBool(SEARCHENGINE);
        static bool serverBasedSearch = Config_getBool(SERVER_BASED_SEARCH);
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

static void writeDefaultQuickLinks(FTextStream &t,bool compact,
                                   HighlightedItem hli,
                                   const char *file,
                                   const QCString &relPath)
{
  static bool serverBasedSearch = Config_getBool(SERVER_BASED_SEARCH);
  static bool searchEngine = Config_getBool(SEARCHENGINE);
  static bool externalSearch = Config_getBool(EXTERNAL_SEARCH);
  LayoutNavEntry *root = LayoutDocManager::instance().rootNavEntry();
  LayoutNavEntry::Kind kind = (LayoutNavEntry::Kind)-1;
  LayoutNavEntry::Kind altKind = (LayoutNavEntry::Kind)-1; // fall back for the old layout file
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
    t << "<script type=\"text/javascript\" src=\"" << relPath << "menudata.js\"></script>" << endl;
    t << "<script type=\"text/javascript\" src=\"" << relPath << "menu.js\"></script>" << endl;
    t << "<script type=\"text/javascript\">" << endl;
		t << "/* @license magnet:?xt=urn:btih:cf05388f2679ee054f2beb29a391d25f4e673ac3&amp;dn=gpl-2.0.txt GPL-v2 */\n";
    t << "$(function() {" << endl;
    t << "  initMenu('" << relPath << "',"
      << (searchEngine?"true":"false") << ","
      << (serverBasedSearch?"true":"false") << ",'"
      << searchPage << "','"
      << theTranslator->trSearch() << "');" << endl;
    if (Config_getBool(SEARCHENGINE))
    {
      if (!serverBasedSearch)
      {
        t << "  $(document).ready(function() { init_search(); });\n";
      }
      else
      {
				t << "/* @license magnet:?xt=urn:btih:cf05388f2679ee054f2beb29a391d25f4e673ac3&amp;dn=gpl-2.0.txt GPL-v2 */\n";
        t << "  $(document).ready(function() {\n"
          << "    if ($('.searchresults').length > 0) { searchBox.DOMSearchField().focus(); }\n"
          << "  });\n";
      }
    }
    t << "});" << endl;
		t << "/* @license-end */";
    t << "</script>" << endl;
    t << "<div id=\"main-nav\"></div>" << endl;
  }
  else if (compact) // && !Config_getBool(HTML_DYNAMIC_MENUS)
  {
    // find highlighted index item
    LayoutNavEntry *hlEntry = root->find(kind,kind==LayoutNavEntry::UserGroup ? file : 0);
    if (!hlEntry && altKind!=(LayoutNavEntry::Kind)-1) { hlEntry=root->find(altKind); kind=altKind; }
    if (!hlEntry) // highlighted item not found in the index! -> just show the level 1 index...
    {
      highlightParent=TRUE;
      hlEntry = root->children().getFirst();
      if (hlEntry==0)
      {
        return; // argl, empty index!
      }
    }
    if (kind==LayoutNavEntry::UserGroup)
    {
      LayoutNavEntry *e = hlEntry->children().getFirst();
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
  t << "</div><!-- top -->" << endl;
}

QCString HtmlGenerator::writeSplitBarAsString(const char *name,const char *relpath)
{
  static bool generateTreeView = Config_getBool(GENERATE_TREEVIEW);
  QCString result;
  // write split bar
  if (generateTreeView)
  {
    result = QCString(
											"<div id=\"side-nav\" class=\"ui-resizable side-nav-resizable\">\n"
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
											"/* @license magnet:?xt=urn:btih:cf05388f2679ee054f2beb29a391d25f4e673ac3&amp;dn=gpl-2.0.txt GPL-v2 */\n"
											"$(document).ready(function(){initNavTree('") +
			QCString(name) + Doxygen::htmlFileExtension +
			QCString("','") + relpath +
			QCString("');});\n"
							 "/* @license-end */\n"
							 "</script>\n"
							 "<div id=\"doc-content\">\n");
  }
  return result;
}

void HtmlGenerator::writeSplitBar(const char *name)
{
  t << writeSplitBarAsString(name,relPath);
}

void HtmlGenerator::writeNavigationPath(const char *s)
{
  t << substitute(s,"$relpath^",relPath);
}

void HtmlGenerator::startContents()
{
  t << "<div class=\"contents\">" << endl;
}

void HtmlGenerator::endContents()
{
  t << "</div><!-- contents -->" << endl;
}

void HtmlGenerator::startPageDoc(const char *pageTitle)
{
  t << "<div" << getDirHtmlClassOfPage(pageTitle) << ">";
}

void HtmlGenerator::endPageDoc()
{
  t << "</div><!-- PageDoc -->" << endl;
}

void HtmlGenerator::writeQuickLinks(bool compact,HighlightedItem hli,const char *file)
{
  writeDefaultQuickLinks(t,compact,hli,file,relPath);
}

// PHP based search script
void HtmlGenerator::writeSearchPage()
{
  static bool generateTreeView = Config_getBool(GENERATE_TREEVIEW);
  static bool disableIndex = Config_getBool(DISABLE_INDEX);
  static QCString projectName = Config_getString(PROJECT_NAME);
  static QCString htmlOutput = Config_getString(HTML_OUTPUT);

  // OPENSEARCH_PROVIDER {
  QCString configFileName = htmlOutput+"/search_config.php";
  QFile cf(configFileName);
  if (cf.open(IO_WriteOnly))
  {
    FTextStream t(&cf);
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

  ResourceMgr::instance().copyResource("search_functions.php",htmlOutput);
  ResourceMgr::instance().copyResource("search_opensearch.php",htmlOutput);
  // OPENSEARCH_PROVIDER }

  QCString fileName = htmlOutput+"/search.php";
  QFile f(fileName);
  if (f.open(IO_WriteOnly))
  {
    FTextStream t(&f);
    t << substituteHtmlKeywords(g_header,"Search","");

    t << "<!-- " << theTranslator->trGeneratedBy() << " Doxygen "
      << versionString << " -->" << endl;
    t << "<script type=\"text/javascript\">\n";
		t << "/* @license magnet:?xt=urn:btih:cf05388f2679ee054f2beb29a391d25f4e673ac3&amp;dn=gpl-2.0.txt GPL-v2 */\n";
		t << "var searchBox = new SearchBox(\"searchBox\", \""
      << "search\",false,'" << theTranslator->trSearch() << "');\n";
		t << "/* @license-end */\n";
    t << "</script>\n";
    if (!Config_getBool(DISABLE_INDEX))
    {
      writeDefaultQuickLinks(t,TRUE,HLI_Search,0,"");
    }
    else
    {
      t << "</div>" << endl;
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
  QCString scriptName = htmlOutput+"/search/search.js";
  QFile sf(scriptName);
  if (sf.open(IO_WriteOnly))
  {
    FTextStream t(&sf);
    t << ResourceMgr::instance().getAsString("extsearch.js");
  }
  else
  {
     err("Failed to open file '%s' for writing...\n",scriptName.data());
  }
}

void HtmlGenerator::writeExternalSearchPage()
{
  static bool generateTreeView = Config_getBool(GENERATE_TREEVIEW);
  static bool disableIndex = Config_getBool(DISABLE_INDEX);
  QCString fileName = Config_getString(HTML_OUTPUT)+"/search"+Doxygen::htmlFileExtension;
  QFile f(fileName);
  if (f.open(IO_WriteOnly))
  {
    FTextStream t(&f);
    t << substituteHtmlKeywords(g_header,"Search","");

    t << "<!-- " << theTranslator->trGeneratedBy() << " Doxygen "
      << versionString << " -->" << endl;
    t << "<script type=\"text/javascript\">\n";
		t << "/* @license magnet:?xt=urn:btih:cf05388f2679ee054f2beb29a391d25f4e673ac3&amp;dn=gpl-2.0.txt GPL-v2 */\n";
		t << "var searchBox = new SearchBox(\"searchBox\", \""
      << "search\",false,'" << theTranslator->trSearch() << "');\n";
		t << "/* @license-end */\n";
    t << "</script>\n";
    if (!Config_getBool(DISABLE_INDEX))
    {
      writeDefaultQuickLinks(t,TRUE,HLI_Search,0,"");
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
      t << "</div>" << endl;
    }
    t << writeSplitBarAsString("search","");
    t << "<div class=\"header\">" << endl;
    t << "  <div class=\"headertitle\">" << endl;
    t << "    <div class=\"title\">" << theTranslator->trSearchResultsTitle() << "</div>" << endl;
    t << "  </div>" << endl;
    t << "</div>" << endl;
    t << "<div class=\"contents\">" << endl;

    t << "<div id=\"searchresults\"></div>" << endl;
    t << "</div>" << endl;

    if (generateTreeView)
    {
      t << "</div><!-- doc-content -->" << endl;
    }

    writePageFooter(t,"Search","","");

  }
  QCString scriptName = Config_getString(HTML_OUTPUT)+"/search/search.js";
  QFile sf(scriptName);
  if (sf.open(IO_WriteOnly))
  {
    FTextStream t(&sf);
    t << "var searchResultsText=["
      << "\"" << theTranslator->trSearchResults(0) << "\","
      << "\"" << theTranslator->trSearchResults(1) << "\","
      << "\"" << theTranslator->trSearchResults(2) << "\"];" << endl;
    t << "var serverUrl=\"" << Config_getString(SEARCHENGINE_URL) << "\";" << endl;
    t << "var tagMap = {" << endl;
    bool first=TRUE;
    // add search mappings
    QStrList &extraSearchMappings = Config_getList(EXTRA_SEARCH_MAPPINGS);
    char *ml=extraSearchMappings.first();
    while (ml)
    {
      QCString mapLine = ml;
      int eqPos = mapLine.find('=');
      if (eqPos!=-1) // tag command contains a destination
      {
        QCString tagName = mapLine.left(eqPos).stripWhiteSpace();
        QCString destName = mapLine.right(mapLine.length()-eqPos-1).stripWhiteSpace();
        if (!tagName.isEmpty())
        {
          if (!first) t << "," << endl;
          t << "  \"" << tagName << "\": \"" << destName << "\"";
          first=FALSE;
        }
      }
      ml=extraSearchMappings.next();
    }
    if (!first) t << endl;
    t << "};" << endl << endl;
    t << ResourceMgr::instance().getAsString("extsearch.js");
    t << endl;
    t << "$(document).ready(function() {" << endl;
    t << "  var query = trim(getURLParameter('query'));" << endl;
    t << "  if (query) {" << endl;
    t << "    searchFor(query,0,20);" << endl;
    t << "  } else {" << endl;
    t << "    var results = $('#results');" << endl;
    t << "    results.html('<p>" << theTranslator->trSearchResults(0) << "</p>');" << endl;
    t << "  }" << endl;
    t << "});" << endl;
  }
  else
  {
     err("Failed to open file '%s' for writing...\n",scriptName.data());
  }
}

void HtmlGenerator::startConstraintList(const char *header)
{
  t << "<div class=\"typeconstraint\">" << endl;
  t << "<dl><dt><b>" << header << "</b></dt><dd>" << endl;
  t << "<table border=\"0\" cellspacing=\"2\" cellpadding=\"0\">" << endl;
}

void HtmlGenerator::startConstraintParam()
{
  t << "<tr><td valign=\"top\"><em>";
}

void HtmlGenerator::endConstraintParam()
{
  t << "</em></td>";
}

void HtmlGenerator::startConstraintType()
{
  t << "<td>&#160;:</td><td valign=\"top\"><em>";
}

void HtmlGenerator::endConstraintType()
{
  t << "</em></td>";
}

void HtmlGenerator::startConstraintDocs()
{
  t << "<td>&#160;";
}

void HtmlGenerator::endConstraintDocs()
{
  t << "</td></tr>" << endl;
}

void HtmlGenerator::endConstraintList()
{
  t << "</table>" << endl;
  t << "</dd>" << endl;
  t << "</dl>" << endl;
  t << "</div>" << endl;
}

void HtmlGenerator::lineBreak(const char *style)
{
  if (style)
  {
    t << "<br class=\"" << style << "\" />" << endl;
  }
  else
  {
    t << "<br />" << endl;
  }
}

void HtmlGenerator::startHeaderSection()
{
  t << "<div class=\"header\">" << endl;
}

void HtmlGenerator::startTitleHead(const char *)
{
  t << "  <div class=\"headertitle\">" << endl;
  startTitle();
}

void HtmlGenerator::endTitleHead(const char *,const char *)
{
  endTitle();
  t << "  </div>" << endl;
}

void HtmlGenerator::endHeaderSection()
{
  t << "</div><!--header-->" << endl;
}

void HtmlGenerator::startInlineHeader()
{
  if (m_emptySection)
  {
    t << "<table class=\"memberdecls\">" << endl;
    m_emptySection=FALSE;
  }
  t << "<tr><td colspan=\"2\"><h3>";
}

void HtmlGenerator::endInlineHeader()
{
  t << "</h3></td></tr>" << endl;
}

void HtmlGenerator::startMemberDocSimple(bool isEnum)
{
  DBG_HTML(t << "<!-- startMemberDocSimple -->" << endl;)
  t << "<table class=\"fieldtable\">" << endl;
  t << "<tr><th colspan=\"" << (isEnum?"2":"3") << "\">";
  t << (isEnum? theTranslator->trEnumerationValues() :
       theTranslator->trCompoundMembers()) << "</th></tr>" << endl;
}

void HtmlGenerator::endMemberDocSimple(bool)
{
  DBG_HTML(t << "<!-- endMemberDocSimple -->" << endl;)
  t << "</table>" << endl;
}

void HtmlGenerator::startInlineMemberType()
{
  DBG_HTML(t << "<!-- startInlineMemberType -->" << endl;)
  t << "<tr><td class=\"fieldtype\">" << endl;
}

void HtmlGenerator::endInlineMemberType()
{
  DBG_HTML(t << "<!-- endInlineMemberType -->" << endl;)
  t << "</td>" << endl;
}

void HtmlGenerator::startInlineMemberName()
{
  DBG_HTML(t << "<!-- startInlineMemberName -->" << endl;)
  t << "<td class=\"fieldname\">" << endl;
}

void HtmlGenerator::endInlineMemberName()
{
  DBG_HTML(t << "<!-- endInlineMemberName -->" << endl;)
  t << "</td>" << endl;
}

void HtmlGenerator::startInlineMemberDoc()
{
  DBG_HTML(t << "<!-- startInlineMemberDoc -->" << endl;)
  t << "<td class=\"fielddoc\">" << endl;
}

void HtmlGenerator::endInlineMemberDoc()
{
  DBG_HTML(t << "<!-- endInlineMemberDoc -->" << endl;)
  t << "</td></tr>" << endl;
}

void HtmlGenerator::startLabels()
{
  DBG_HTML(t << "<!-- startLabels -->" << endl;)
  t << "<span class=\"mlabels\">";
}

void HtmlGenerator::writeLabel(const char *l,bool /*isLast*/)
{
  DBG_HTML(t << "<!-- writeLabel(" << l << ") -->" << endl;)
  //t << "<tt>[" << l << "]</tt>";
  //if (!isLast) t << ", ";
  t << "<span class=\"mlabel\">" << l << "</span>";
}

void HtmlGenerator::endLabels()
{
  DBG_HTML(t << "<!-- endLabels -->" << endl;)
  t << "</span>";
}

void HtmlGenerator::writeInheritedSectionTitle(
                  const char *id,    const char *ref,
                  const char *file,  const char *anchor,
                  const char *title, const char *name)
{
  DBG_HTML(t << "<!-- writeInheritedSectionTitle -->" << endl;)
  QCString a = anchor;
  if (!a.isEmpty()) a.prepend("#");
  QCString classLink = QCString("<a class=\"el\" ");
  if (ref)
  {
    classLink+= externalLinkTarget();
    classLink += " href=\"";
    classLink+= externalRef(relPath,ref,TRUE);
  }
  else
  {
    classLink += "href=\"";
    classLink+=relPath;
  }
  classLink+=file+Doxygen::htmlFileExtension+a;
  classLink+=QCString("\">")+convertToHtml(name,FALSE)+"</a>";
  t << "<tr class=\"inherit_header " << id << "\">"
    << "<td colspan=\"2\" onclick=\"javascript:toggleInherit('" << id << "')\">"
    << "<img src=\"" << relPath << "closed.png\" alt=\"-\"/>&#160;"
    << theTranslator->trInheritedFrom(convertToHtml(title,FALSE),classLink)
    << "</td></tr>" << endl;
}

void HtmlGenerator::writeSummaryLink(const char *file,const char *anchor,const char *title,bool first)
{
  if (first)
  {
    t << "  <div class=\"summary\">\n";
  }
  else
  {
    t << " &#124;\n";
  }
  t << "<a href=\"";
  if (file)
  {
    t << relPath << file;
    t << Doxygen::htmlFileExtension;
  }
  else
  {
    t << "#";
    t << anchor;
  }
  t << "\">";
  t << title;
  t << "</a>";
}

void HtmlGenerator::endMemberDeclaration(const char *anchor,const char *inheritId)
{
  t << "<tr class=\"separator:" << anchor;
  if (inheritId)
  {
    t << " inherit " << inheritId;
  }
  t << "\"><td class=\"memSeparator\" colspan=\"2\">&#160;</td></tr>\n";
}

void HtmlGenerator::setCurrentDoc(Definition *context,const char *anchor,bool isSourceFile)
{
  if (Doxygen::searchIndex)
  {
    Doxygen::searchIndex->setCurrentDoc(context,anchor,isSourceFile);
  }
}

void HtmlGenerator::addWord(const char *word,bool hiPriority)
{
  if (Doxygen::searchIndex)
  {
    Doxygen::searchIndex->addWord(word,hiPriority);
  }
}
