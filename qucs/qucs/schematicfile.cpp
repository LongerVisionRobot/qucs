/***************************************************************************
                              schematic_file.cpp
                             --------------------
    begin                : Sat Mar 27 2004
    copyright            : (C) 2003 by Michael Margraf
    email                : michael.margraf@alumni.tu-berlin.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <QtCore>
#include <QMessageBox>
#include <QDir>
#include <QStringList>
#include <QPlainTextEdit>
#include "qt_compat.h"
#include <QTextStream>
#include <QList>
#include <QProcess>
#include <QDebug>
#include <QApplication>
#include <QClipboard>

#include "qucs.h"
#include "node.h"
#include "schematicfile.h"
#include "schematicview.h"
#include "schematicscene.h"
#include "diagrams/diagrams.h"
#include "paintings/paintings.h"
#include "components.h"
#include "module.h"
#include "misc.h"
#include "textdoc.h"
#include "components/vafile.h" // why not components.h?
#include "frame.h"


// Here the subcircuits, SPICE components etc are collected. It must be
// global to also work within the subcircuits.
SubMap FileList;

SchematicFile::SchematicFile(QObject *parent) :
    QObject(parent)
{
  isVerilog = false;
  creatingLib = false;
}

/*!
 * \brief SchematicFile::setScene
 * \param scn
 * Set scene and associated lists of items.
 * - scene is need o load items into.
 * - Lists of items are need to save back to file.
 */
void SchematicFile::setScene(SchematicScene *scn)
{
    scene = scn;
    scene->Wires        = scn->Wires;
    scene->DocWires     = scn->DocWires;
    scene->Nodes        = scn->Nodes;
    scene->DocNodes     = scn->DocNodes;
    scene->Diagrams     = scn->Diagrams;
    scene->DocDiags     = scn->DocDiags;
    scene->Paintings    = scn->Paintings;
    scene->DocPaints    = scn->DocPaints;
    scene->Components   = scn->Components;
    scene->DocComps     = scn->DocComps;
    scene->SymbolPaints = scn->SymbolPaints;
}

// -------------------------------------------------------------
// Creates a Qucs file format (without document properties) in the returning
// string. This is used to copy the selected elements into the clipboard.
QString SchematicFile::createClipboardFile()
{
  int z=0;  // counts selected elements
  Wire *pw;
  Diagram *pd;
  Painting *pp;
  Component *pc;

  QString s("<Qucs Schematic " PACKAGE_VERSION ">\n");

  // Build element document.
  s += "<Components>\n";
  for(pc = Components->first(); pc != 0; pc = Components->next()){
    if(pc->ElemSelected) {
      QTextStream str(&s);
      saveComponent(str, pc);
      s += "\n";
      ++z;
    }
  }
  s += "</Components>\n";

  s += "<Wires>\n";
  for(pw = Wires->first(); pw != 0; pw = Wires->next())
    if(pw->ElemSelected) {
      z++;
      if(pw->Label) if(!pw->Label->ElemSelected) {
	s += pw->save().section('"', 0, 0)+"\"\" 0 0 0>\n";
	continue;
      }
      s += pw->save()+"\n";
    }
  for(Node *pn = Nodes->first(); pn != 0; pn = Nodes->next())
    if(pn->Label) if(pn->Label->ElemSelected) {
      s += pn->Label->save()+"\n";  z++; }
  s += "</Wires>\n";

  s += "<Diagrams>\n";
  for(pd = Diagrams->first(); pd != 0; pd = Diagrams->next())
    if(pd->ElemSelected) {
      s += pd->save()+"\n";  z++; }
  s += "</Diagrams>\n";

  s += "<Paintings>\n";
  for(pp = Paintings->first(); pp != 0; pp = Paintings->next())
    if(pp->ElemSelected)
      if(pp->Name.at(0) != '.') {  // subcircuit specific -> do not copy
        s += "<"+pp->save()+">\n";  z++; }
  s += "</Paintings>\n";

  if(z == 0) return "";   // return empty if no selection

  return s;
}

// -------------------------------------------------------------
// Only read fields without loading them.
bool SchematicFile::loadIntoNothing(QTextStream *stream)
{
  QString Line, cstr;
  while(!stream->atEnd()) {
    Line = stream->readLine();
    if(Line.at(0) == '<') if(Line.at(1) == '/') return true;
  }

  QMessageBox::critical(0, QObject::tr("Error"),
	QObject::tr("Format Error:\n'Painting' field is not closed!"));
  return false;
}

// -------------------------------------------------------------
// Paste from clipboard.
bool SchematicFile::pasteFromClipboard(QTextStream *stream, Q3PtrList<Element> *pe)
{
  QString Line;

  Line = stream->readLine();
  if(Line.left(16) != "<Qucs Schematic ")   // wrong file type ?
    return false;

  Line = Line.mid(16, Line.length()-17);
  VersionTriplet DocVersion = VersionTriplet(Line);
  if (DocVersion > QucsVersion) { // wrong version number ?
    if (!QucsSettings.IgnoreFutureVersion) {
      QMessageBox::critical(0, QObject::tr("Error"),
                            QObject::tr("Wrong document version: %1").arg(DocVersion.toString()));
      return false;
    }
  }

  // read content in symbol edit mode *************************
  if(symbolMode) {
    while(!stream->atEnd()) {
      Line = stream->readLine();
      if(Line == "<Components>") {
        if(!loadIntoNothing(stream)) return false; }
      else
      if(Line == "<Wires>") {
        if(!loadIntoNothing(stream)) return false; }
      else
      if(Line == "<Diagrams>") {
        if(!loadIntoNothing(stream)) return false; }
      else
      if(Line == "<Paintings>") {
        if(!loadPaintings(stream, (Q3PtrList<Painting>*)pe)) return false; }
      else {
        QMessageBox::critical(0, QObject::tr("Error"),
		   QObject::tr("Clipboard Format Error:\nUnknown field!"));
        return false;
      }
    }

    return true;
  }

  // read content in schematic edit mode *************************
  while(!stream->atEnd()) {
    Line = stream->readLine();
    if(Line == "<Components>") {
      if(!loadComponents(stream, (Q3PtrList<Component>*)pe)) return false; }
    else
    if(Line == "<Wires>") {
      if(!loadWires(stream, pe)) return false; }
    else
    if(Line == "<Diagrams>") {
      if(!loadDiagrams(stream, (Q3PtrList<Diagram>*)pe)) return false; }
    else
    if(Line == "<Paintings>") {
      if(!loadPaintings(stream, (Q3PtrList<Painting>*)pe)) return false; }
    else {
      QMessageBox::critical(0, QObject::tr("Error"),
		   QObject::tr("Clipboard Format Error:\nUnknown field!"));
      return false;
    }
  }

  return true;
}

// -------------------------------------------------------------
int SchematicFile::saveSymbolCpp (void)
{
  QFileInfo info (DocName);
  QString cppfile = info.path () + QDir::separator() + DataSet;
  QFile file (cppfile);

  if (!file.open (QIODevice::WriteOnly)) {
    QMessageBox::critical (0, QObject::tr("Error"),
		   QObject::tr("Cannot save C++ file \"%1\"!").arg(cppfile));
    return -1;
  }

  QTextStream stream (&file);

  // automatically compute boundings of drawing
  int xmin = INT_MAX;
  int ymin = INT_MAX;
  int xmax = INT_MIN;
  int ymax = INT_MIN;
  int x1, y1, x2, y2;
  int maxNum = 0;
  Painting * pp;

  stream << "  // symbol drawing code\n";
  for (pp = SymbolPaints.first (); pp != 0; pp = SymbolPaints.next ()) {
    if (pp->Name == ".ID ") continue;
    if (pp->Name == ".PortSym ") {
      if (((PortSymbol*)pp)->numberStr.toInt() > maxNum)
	maxNum = ((PortSymbol*)pp)->numberStr.toInt();
      x1 = ((PortSymbol*)pp)->cx;
      y1 = ((PortSymbol*)pp)->cy;
      if (x1 < xmin) xmin = x1;
      if (x1 > xmax) xmax = x1;
      if (y1 < ymin) ymin = y1;
      if (y1 > ymax) ymax = y1;
      continue;
    }
    pp->Bounding (x1, y1, x2, y2);
    if (x1 < xmin) xmin = x1;
    if (x2 > xmax) xmax = x2;
    if (y1 < ymin) ymin = y1;
    if (y2 > ymax) ymax = y2;
    stream << "  " << pp->saveCpp () << "\n";
  }

  stream << "\n  // terminal definitions\n";
  for (int i = 1; i <= maxNum; i++) {
    for (pp = SymbolPaints.first (); pp != 0; pp = SymbolPaints.next ()) {
      if (pp->Name == ".PortSym ")
	if (((PortSymbol*)pp)->numberStr.toInt() == i)
	  stream << "  " << pp->saveCpp () << "\n";
    }
  }

  stream << "\n  // symbol boundings\n"
	 << "  x1 = " << xmin << "; " << "  y1 = " << ymin << ";\n"
	 << "  x2 = " << xmax << "; " << "  y2 = " << ymax << ";\n";

  stream << "\n  // property text position\n";
  for (pp = SymbolPaints.first (); pp != 0; pp = SymbolPaints.next ())
    if (pp->Name == ".ID ")
      stream << "  " << pp->saveCpp () << "\n";

  file.close ();
  return 0;
}

// save symbol paintings in JSON format
int SchematicFile::saveSymbolJSON()
{
  QFileInfo info (DocName);
  QString jsonfile = info.path () + QDir::separator()
                   + info.completeBaseName() + "_sym.json";

  qDebug() << "saveSymbolJson for " << jsonfile;

  QFile file (jsonfile);

  if (!file.open (QIODevice::WriteOnly)) {
    QMessageBox::critical (0, QObject::tr("Error"),
		   QObject::tr("Cannot save JSON symbol file \"%1\"!").arg(jsonfile));
    return -1;
  }

  QTextStream stream (&file);

  // automatically compute boundings of drawing
  int xmin = INT_MAX;
  int ymin = INT_MAX;
  int xmax = INT_MIN;
  int ymax = INT_MIN;
  int x1, y1, x2, y2;
  int maxNum = 0;
  Painting * pp;

  stream << "{\n";

  stream << "\"paintings\" : [\n";

  // symbol drawing code"
  for (pp = SymbolPaints.first (); pp != 0; pp = SymbolPaints.next ()) {
    if (pp->Name == ".ID ") continue;
    if (pp->Name == ".PortSym ") {
      if (((PortSymbol*)pp)->numberStr.toInt() > maxNum)
	maxNum = ((PortSymbol*)pp)->numberStr.toInt();
      x1 = ((PortSymbol*)pp)->cx;
      y1 = ((PortSymbol*)pp)->cy;
      if (x1 < xmin) xmin = x1;
      if (x1 > xmax) xmax = x1;
      if (y1 < ymin) ymin = y1;
      if (y1 > ymax) ymax = y1;
      continue;
    }
    pp->Bounding (x1, y1, x2, y2);
    if (x1 < xmin) xmin = x1;
    if (x2 > xmax) xmax = x2;
    if (y1 < ymin) ymin = y1;
    if (y2 > ymax) ymax = y2;
    stream << "  " << pp->saveJSON() << "\n";
  }

  // terminal definitions
  //stream << "terminal \n";
  for (int i = 1; i <= maxNum; i++) {
    for (pp = SymbolPaints.first (); pp != 0; pp = SymbolPaints.next ()) {
      if (pp->Name == ".PortSym ")
	if (((PortSymbol*)pp)->numberStr.toInt() == i)
	  stream << "  " << pp->saveJSON () << "\n";
    }
  }

  stream << "],\n"; //end of paintings JSON array

  // symbol boundings
  stream
    << "  \"x1\" : " << xmin << ",\n" << "  \"y1\" : " << ymin << ",\n"
    << "  \"x2\" : " << xmax << ",\n" << "  \"y2\" : " << ymax << ",\n";

  // property text position
  for (pp = SymbolPaints.first (); pp != 0; pp = SymbolPaints.next ())
    if (pp->Name == ".ID ")
      stream << "  " << pp->saveJSON () << "\n";

  stream << "}\n";

  file.close ();
  return 0;


}

// -------------------------------------------------------------
// Returns the number of subcircuit ports.
int SchematicFile::saveDocument()
{
  QFile file(DocName);
  if(!file.open(QIODevice::WriteOnly)) {
    QMessageBox::critical(0, QObject::tr("Error"),
				QObject::tr("Cannot save document!"));
    return -1;
  }

  QTextStream stream(&file);

  stream << "<Qucs Schematic " << PACKAGE_VERSION << ">\n";

  stream << "<Properties>\n";
  if(symbolMode) {
    stream << "  <View=" << tmpViewX1<<","<<tmpViewY1<<","
			 << tmpViewX2<<","<<tmpViewY2<< ",";
    stream <<tmpScale<<","<<tmpPosX<<","<<tmpPosY << ">\n";
  }
  else {
    stream << "  <View=" << ViewX1<<","<<ViewY1<<","
                         << ViewX2<<","<<ViewY2<< ",";
    TODO("Fix contentsX");
    /// \todo  stream << Scale <<","<<contentsX()<<","<<contentsY() << ">\n";
    stream << Scale <<","<< 0 <<","<< 0 << ">\n";
  }
  stream << "  <Grid=" << GridX<<","<<GridY<<","
			<< GridOn << ">\n";
  stream << "  <DataSet=" << DataSet << ">\n";
  stream << "  <DataDisplay=" << DataDisplay << ">\n";
  stream << "  <OpenDisplay=" << SimOpenDpl << ">\n";
  stream << "  <Script=" << Script << ">\n";
  stream << "  <RunScript=" << SimRunScript << ">\n";
  stream << "  <showFrame=" << scene->schematicFrame->PageType << ">\n";

  QString t;
  misc::convert2ASCII(t = scene->schematicFrame->Title);
  stream << "  <FrameText0=" << t << ">\n";
  misc::convert2ASCII(t = scene->schematicFrame->Author);
  stream << "  <FrameText1=" << t << ">\n";
  misc::convert2ASCII(t = scene->schematicFrame->Date);
  stream << "  <FrameText2=" << t << ">\n";
  misc::convert2ASCII(t = scene->schematicFrame->Revision);
  stream << "  <FrameText3=" << t << ">\n";
  stream << "</Properties>\n";

  Painting *pp;
  stream << "<Symbol>\n";     // save all paintings for symbol
  for(pp = SymbolPaints.first(); pp != 0; pp = SymbolPaints.next())
    stream << "  <" << pp->save() << ">\n";
  stream << "</Symbol>\n";

  stream << "<Components>\n";    // save all components
  for(Component *pc = DocComps.first(); pc != 0; pc = DocComps.next()){
    stream << "  "; // BUG language specific.
    saveComponent(stream, pc);
    stream << "\n"; // BUG?
  }
  stream << "</Components>\n";

  stream << "<Wires>\n";    // save all wires
  for(Wire *pw = DocWires.first(); pw != 0; pw = DocWires.next())
    stream << "  " << pw->save() << "\n";

  // save all labeled nodes as wires
  for(Node *pn = DocNodes.first(); pn != 0; pn = DocNodes.next())
    if(pn->Label) stream << "  " << pn->Label->save() << "\n";
  stream << "</Wires>\n";

  stream << "<Diagrams>\n";    // save all diagrams
  for(Diagram *pd = DocDiags.first(); pd != 0; pd = DocDiags.next())
    stream << "  " << pd->save() << "\n";
  stream << "</Diagrams>\n";

  stream << "<Paintings>\n";     // save all paintings
  for(pp = DocPaints.first(); pp != 0; pp = DocPaints.next())
    stream << "  <" << pp->save() << ">\n";
  stream << "</Paintings>\n";

  file.close();

  // additionally save symbol C++ code if in a symbol drawing and the
  // associated file is a Verilog-A file

  TODO("need a qucsdoc ref here")
  QucsDoc *doc = new QucsDoc(0,QString());
  if (doc->fileSuffix () == "sym") {
    if (doc->fileSuffix (DataDisplay) == "va") {
      saveSymbolCpp ();
      saveSymbolJSON ();

      // TODO slit this into another method, or merge into saveSymbolJSON
      // handle errors in separate
      qDebug() << "  -> Run adms for symbol";

      QString vaFile;

//      QDir prefix = QDir(QucsSettings.BinDir);

      QDir include = QDir(QucsSettings.BinDir+"../include/qucs-core");

      //pick admsXml from settings
      QString admsXml = QucsSettings.AdmsXmlBinDir.canonicalPath();

#ifdef __MINGW32__
      admsXml = QDir::toNativeSeparators(admsXml+"/"+"admsXml.exe");
#else
      admsXml = QDir::toNativeSeparators(admsXml+"/"+"admsXml");
#endif
      // BUG: duplicated from qucs_actions.cpp
      char const* var = getenv("QUCS_USE_PATH");
      if(var != NULL) {
	// just use PATH. this is currently bound to a contition, to maintain
	// backwards compatibility with QUCSDIR
	qDebug() << "QUCS_USE_PATH";
	admsXml = "admsXml";
      }else{
      }

      QString workDir = QucsSettings.QucsWorkDir.absolutePath();

      qDebug() << "App path : " << qApp->applicationDirPath();
      qDebug() << "workdir"  << workDir;
      qDebug() << "homedir"  << QucsSettings.QucsHomeDir.absolutePath();
      qDebug() << "projsdir"  << QucsSettings.projsDir.absolutePath();

      vaFile = QucsSettings.QucsWorkDir.filePath(doc->fileBase()+".va");

      QStringList Arguments;
      Arguments << QDir::toNativeSeparators(vaFile)
                << "-I" << QDir::toNativeSeparators(include.absolutePath())
                << "-e" << QDir::toNativeSeparators(include.absoluteFilePath("qucsMODULEguiJSONsymbol.xml"))
                << "-A" << "dyload";

//      QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

      QFile file(admsXml);
      if(var) {
	// don't do this. it will always report an error.
      }else if ( !file.exists() ){
        QMessageBox(QMessageBox::Critical,
                    tr("Error"),
                    tr("Program admsXml not found: %1\n\n"
                       "Set the admsXml location on the application settings.").arg(admsXml));
        return -1;
      }

      qDebug() << "Command: " << admsXml << Arguments.join(" ");

      // need to cd into project to run admsXml?
      QDir::setCurrent(workDir);

      QProcess builder;
      builder.setProcessChannelMode(QProcess::MergedChannels);

      builder.start(admsXml, Arguments);


      // how to capture [warning]? need to modify admsXml?
      // TODO put stdout, stderr into a dock window, not messagebox
      if (!builder.waitForFinished()) {
        QString cmdString = QString("%1 %2\n\n").arg(admsXml, Arguments.join(" "));
        cmdString = cmdString + builder.errorString();
        QMessageBox(QMessageBox::Critical,
                    tr("Error"),
                    cmdString);
      }
      else {
        QString cmdString = QString("%1 %2\n\n").arg(admsXml, Arguments.join(" "));
        cmdString = cmdString + builder.readAll();
        QMessageBox(QMessageBox::Information,
                    tr("Status"),
                    cmdString);
      }

      // Append _sym.json into _props.json, save into _symbol.json
      QFile f1(QucsSettings.QucsWorkDir.filePath(doc->fileBase()+"_props.json"));
      QFile f2(QucsSettings.QucsWorkDir.filePath(doc->fileBase()+"_sym.json"));
      f1.open(QIODevice::ReadOnly | QIODevice::Text);
      f2.open(QIODevice::ReadOnly | QIODevice::Text);

      QString dat1 = QString(f1.readAll());
      QString dat2 = QString(f2.readAll());
      QString finalJSON = dat1.append(dat2);

      // remove joining point
      finalJSON = finalJSON.replace("}{", "");

      QFile f3(QucsSettings.QucsWorkDir.filePath(doc->fileBase()+"_symbol.json"));
      f3.open(QIODevice::WriteOnly | QIODevice::Text);
      QTextStream out(&f3);
      out << finalJSON;

      f1.close();
      f2.close();
      f3.close();

      // TODO choose icon, default to something or provided png

    } // if DataDisplay va
  } // if suffix .sym

  return 0;
}

// -------------------------------------------------------------
bool SchematicFile::loadProperties(QTextStream *stream)
{
  bool ok = true;
  QString Line, cstr, nstr;
  while(!stream->atEnd()) {
    Line = stream->readLine();
    if(Line.at(0) == '<') if(Line.at(1) == '/') return true;  // field end ?
    Line = Line.trimmed();
    if(Line.isEmpty()) continue;

    if(Line.at(0) != '<') {
      QMessageBox(QMessageBox::Critical,
                  QObject::tr("Error"),
                  QObject::tr("Format Error:\nWrong property field limiter!"));
      return false;
    }
    if(Line.at(Line.length()-1) != '>') {
      QMessageBox(QMessageBox::Critical,
                  QObject::tr("Error"),
		          QObject::tr("Format Error:\nWrong property field limiter!"));
      return false;
    }
    Line = Line.mid(1, Line.length()-2);   // cut off start and end character

    cstr = Line.section('=',0,0);    // property type
    nstr = Line.section('=',1,1);    // property value
         if(cstr == "View") {
		ViewX1 = nstr.section(',',0,0).toInt(&ok); if(ok) {
		ViewY1 = nstr.section(',',1,1).toInt(&ok); if(ok) {
		ViewX2 = nstr.section(',',2,2).toInt(&ok); if(ok) {
		ViewY2 = nstr.section(',',3,3).toInt(&ok); if(ok) {
		Scale  = nstr.section(',',4,4).toDouble(&ok); if(ok) {
		tmpViewX1 = nstr.section(',',5,5).toInt(&ok); if(ok)
		tmpViewY1 = nstr.section(',',6,6).toInt(&ok); }}}}} }
    else if(cstr == "Grid") {
		GridX = nstr.section(',',0,0).toInt(&ok); if(ok) {
		GridY = nstr.section(',',1,1).toInt(&ok); if(ok) {
		if(nstr.section(',',2,2).toInt(&ok) == 0) GridOn = false;
		else GridOn = true; }} }
    else if(cstr == "DataSet") DataSet = nstr;
    else if(cstr == "DataDisplay") DataDisplay = nstr;
    else if(cstr == "OpenDisplay")
		if(nstr.toInt(&ok) == 0) SimOpenDpl = false;
		else SimOpenDpl = true;
    else if(cstr == "Script") Script = nstr;
    else if(cstr == "RunScript")
		if(nstr.toInt(&ok) == 0) SimRunScript = false;
		else SimRunScript = true;

    else if(cstr == "showFrame")
                scene->schematicFrame->PageType= nstr.at(0).toLatin1() - '0';
    else if(cstr == "FrameText0") misc::convert2Unicode(scene->schematicFrame->Title = nstr);
    else if(cstr == "FrameText1") misc::convert2Unicode(scene->schematicFrame->Author = nstr);
    else if(cstr == "FrameText2") misc::convert2Unicode(scene->schematicFrame->Date = nstr);
    else if(cstr == "FrameText3") misc::convert2Unicode(scene->schematicFrame->Revision= nstr);
    else {
       QMessageBox(QMessageBox::Critical,
                   tr("Error"),
                   QObject::tr("Format Error:\nUnknown property: ")+cstr);
      return false;
    }
    if(!ok) {
       QMessageBox(QMessageBox::Critical,
                   tr("Error"),
	               QObject::tr("Format Error:\nNumber expected in property field!"));
      return false;
    }
  }

  QMessageBox(QMessageBox::Critical,
              tr("Error"),
              QObject::tr("Format Error:\n'Property' field is not closed!"));
  return false;
}

/*!
 * @brief Schematic::simpleInsertComponent
 *
 * Inserts a component without performing logic for wire optimization.
 * Used when loading from a schematic file.
 *
 * @param c is pointing to the component to be inserted.
 */
void SchematicFile::simpleInsertComponent(Component *c)
{
  Node *pn;
  int x, y;
  // connect every node of component
  foreach(Port *pp, c->Ports) {
    x = pp->x+c->cx;
    y = pp->y+c->cy;

    // check if new node lies upon existing node
    for(pn = DocNodes.first(); pn != 0; pn = DocNodes.next())
      if(pn->cx == x) if(pn->cy == y) {
	if (!pn->DType.isEmpty()) {
	  pp->Type = pn->DType;
	}
	if (!pp->Type.isEmpty()) {
	  pn->DType = pp->Type;
	}
	break;
      }

    if(pn == 0) { // create new node, if no existing one lies at this position
      pn = new Node(x, y);
      DocNodes.append(pn);

      // add Node to scene
      scene->addItem(pn);
    }
    pn->Connections.append(c);  // connect schematic node to component node
    if (!pp->Type.isEmpty()) {
      pn->DType = pp->Type;
    }

    pp->Connection = pn;  // connect component node to schematic node
  }

  scene->DocComps.append(c);
  // add Component to scene
  scene->addItem(c);
  scene->update();
}

// -------------------------------------------------------------
/*!
 * \brief Schematic::loadComponents
 * \param stream File being loaded
 * \param List add read Component to a list of pointers
 * \return Error if Component fielt could not be open
 *
 * Parse components from the input stream.
 * If a list pointer is provided, a list of pointers to Components
 * ( comming from paste? ) is created and passed by reference.
 * Otherwise insert component into database and graphics scene.
 *
 */
bool SchematicFile::loadComponents(QTextStream *stream, Q3PtrList<Component> *List)
{
  QString Line, cstr;
  Component *c;
  while(!stream->atEnd()) {
    Line = stream->readLine();
    if(Line.at(0) == '<') if(Line.at(1) == '/') return true;
    Line = Line.trimmed();
    if(Line.isEmpty()) continue;

    /// \todo enable user to load partial schematic, skip unknown components
    c = getComponentFromName(Line);
    if(!c) return false;

    // set component location
    QPointF center(c->cx, c->cy);
    c->setPos(center);

    if(List) {  // "paste" ?
      int z;
      for(z=c->name().length()-1; z>=0; z--) // cut off number of component name
        if(!c->name().at(z).isDigit()) break;
      c->obsolete_name_override_hack(c->name().left(z+1));
      List->append(c);
    }
    else  {
      // insert component into database and scene
      simpleInsertComponent(c);
    }
  }

  QMessageBox(QMessageBox::Critical,
              tr("Error"),
	          QObject::tr("Format Error:\n'Component' field is not closed!"));
  return false;
}

/*!
 * \brief Schematic::simpleInsertWire
 * \param pw
 * Inserts a wire without performing logic for optimizing.
 * \todo similar to insertNode
 *
 * - check for node 1 overlap
 * - check for zero length wire
 * - add node label (at wire corner) to scene
 * - check for node 2 overlap
 * - add nodes to list
 * - add nodes to scene
 * - add wire to list
 * - add wire to scene
 * - add wire label to scene
 *
 */
void SchematicFile::simpleInsertWire(Wire *pw)
{
  Node *pn1 = 0;
  Node *pn2 = 0;

  // check if first wire node lies upon existing node
  for(pn1 = DocNodes.first(); pn1 != 0; pn1 = DocNodes.next())
    if(pn1->cx == pw->x1) if(pn1->cy == pw->y1) break;

  // create new node, if no existing one lies at this position
  if(!pn1) {
    pn1 = new Node(pw->x1, pw->y1);
    DocNodes.append(pn1);
  }

  // check for zero length wire
  if(pw->x1 == pw->x2) if(pw->y1 == pw->y2) {
    pn1->Label = pw->Label;   // wire with length zero are just node labels
    if (pn1->Label) {
      pn1->Label->ElemType = isNodeLabel;
      pn1->Label->pOwner = pn1;
      // add WireLabel to scene
      scene->addItem(pn1->Label);
    }
    delete pw;           // delete wire because this is not a wire
    return;
  }
  pn1->Connections.append(pw);  // connect schematic node to component node
  pw->Port1 = pn1;

  // check if second wire node lies upon existing node
  for(pn2 = DocNodes.first(); pn2 != 0; pn2 = DocNodes.next())
    if(pn2->cx == pw->x2) if(pn2->cy == pw->y2) break;

  if(!pn2) {   // create new node, if no existing one lies at this position
    pn2 = new Node(pw->x2, pw->y2);
    DocNodes.append(pn2);
  }
  pn2->Connections.append(pw);  // connect schematic node to component node
  pw->Port2 = pn2;

  DocWires.append(pw);

  scene->addItem(pn1);
  scene->addItem(pn2);
  scene->addItem(pw);

  if(pw->Label) {
    scene->addItem(pw->Label);
  }
}

// -------------------------------------------------------------
bool SchematicFile::loadWires(QTextStream *stream, Q3PtrList<Element> *List)
{
  Wire *w;
  QString Line;
  while(!stream->atEnd()) {
    Line = stream->readLine();
    if(Line.at(0) == '<') if(Line.at(1) == '/') return true;
    Line = Line.trimmed();
    if(Line.isEmpty()) continue;

    // (Node*)4 =  move all ports (later on)
    w = new Wire(0,0,0,0, (Node*)4,(Node*)4);
    if(!w->load(Line)) {
      QMessageBox::critical(0, QObject::tr("Error"),
		QObject::tr("Format Error:\nWrong 'wire' line format!"));
      delete w;
      return false;
    }
    if(List) {
      if(w->x1 == w->x2) if(w->y1 == w->y2) if(w->Label) {
        w->Label->ElemType = isMovingLabel;
	List->append(w->Label);
	delete w;
	continue;
      }
      List->append(w);
      if(w->Label)  List->append(w->Label);
    }
    else simpleInsertWire(w);
  }

  QMessageBox::critical(0, QObject::tr("Error"),
		QObject::tr("Format Error:\n'Wire' field is not closed!"));
  return false;
}

// -------------------------------------------------------------
bool SchematicFile::loadDiagrams(QTextStream *stream, Q3PtrList<Diagram> *List)
{
  Diagram *d;
  QString Line, cstr;
  while(!stream->atEnd()) {
    Line = stream->readLine();
    if(Line.at(0) == '<') if(Line.at(1) == '/') return true;
    Line = Line.trimmed();
    if(Line.isEmpty()) continue;

    cstr = Line.section(' ',0,0);    // diagram type
         if(cstr == "<Rect") d = new RectDiagram();
    else if(cstr == "<Polar") d = new PolarDiagram();
    else if(cstr == "<Tab") d = new TabDiagram();
    else if(cstr == "<Smith") d = new SmithDiagram();
    else if(cstr == "<ySmith") d = new SmithDiagram(0,0,false);
    else if(cstr == "<PS") d = new PSDiagram();
    else if(cstr == "<SP") d = new PSDiagram(0,0,false);
    else if(cstr == "<Rect3D") d = new Rect3DDiagram();
    else if(cstr == "<Curve") d = new CurveDiagram();
    else if(cstr == "<Time") d = new TimingDiagram();
    else if(cstr == "<Truth") d = new TruthDiagram();
    /*else if(cstr == "<Phasor") d = new PhasorDiagram();
    else if(cstr == "<Waveac") d = new Waveac();*/
    else {
      QMessageBox::critical(0, QObject::tr("Error"),
		   QObject::tr("Format Error:\nUnknown diagram!"));
      return false;
    }

    if(!d->load(Line, stream)) {
      QMessageBox::critical(0, QObject::tr("Error"),
		QObject::tr("Format Error:\nWrong 'diagram' line format!"));
      delete d;
      return false;
    }
    List->append(d);

  qDebug() << __FUNCTION__ << d->Name;
  /// insert diagram into the scene
  scene->addItem(d);
  scene->update();
  }

  QMessageBox::critical(0, QObject::tr("Error"),
	       QObject::tr("Format Error:\n'Diagram' field is not closed!"));
  return false;
}

// -------------------------------------------------------------
bool SchematicFile::loadPaintings(QTextStream *stream, Q3PtrList<Painting> *List)
{
  Painting *p=0;
  QString Line, cstr;
  while(!stream->atEnd()) {
    Line = stream->readLine();
    if(Line.at(0) == '<') if(Line.at(1) == '/') return true;

    Line = Line.trimmed();
    if(Line.isEmpty()) continue;
    if( (Line.at(0) != '<') || (Line.at(Line.length()-1) != '>')) {
      QMessageBox::critical(0, QObject::tr("Error"),
	QObject::tr("Format Error:\nWrong 'painting' line delimiter!"));
      return false;
    }
    Line = Line.mid(1, Line.length()-2);  // cut off start and end character

    cstr = Line.section(' ',0,0);    // painting type
         if(cstr == "Line") p = new GraphicLine();
    else if(cstr == "EArc") p = new EllipseArc();
    else if(cstr == ".PortSym") p = new PortSymbol();
    else if(cstr == ".ID") p = new ID_Text();
    else if(cstr == "Text") p = new GraphicText();
    else if(cstr == "Rectangle") p = new Rectangle();
    else if(cstr == "Arrow") p = new Arrow();
    else if(cstr == "Ellipse") p = new Ellipse();
    else {
      QMessageBox::critical(0, QObject::tr("Error"),
		QObject::tr("Format Error:\nUnknown painting!"));
      return false;
    }

    if(!p->load(Line)) {
      QMessageBox::critical(0, QObject::tr("Error"),
	QObject::tr("Format Error:\nWrong 'painting' line format!"));
      delete p;
      return false;
    }
    List->append(p);

    qDebug() << __FUNCTION__ << p->Name;
    /// insert Paiting into the scene
    scene->addItem(p);
    scene->update();
  }

  QMessageBox::critical(0, QObject::tr("Error"),
	QObject::tr("Format Error:\n'Painting' field is not closed!"));
  return false;
}

/*!
 * \brief Schematic::loadDocument tries to load a schematic document.
 * \return true/false in case of success/failure
 */
bool SchematicFile::loadDocument()
{
  QFile file(DocName);
  if(!file.open(QIODevice::ReadOnly)) {
    /// \todo implement unified error/warning handling GUI and CLI
    if (QucsMain)
      QMessageBox::critical(0, QObject::tr("Error"),
                 QObject::tr("Cannot load document: ")+DocName);
    else
      qCritical() << "Schematic::loadDocument:"
                  << QObject::tr("Cannot load document: ")+DocName;
    return false;
  }

  // Keep reference to source file (the schematic file)
  scene->setFileInfo(DocName);

  QString Line;
  QTextStream stream(&file);

  // read header **************************
  do {
    if(stream.atEnd()) {
      file.close();
      return true;
    }

    Line = stream.readLine();
  } while(Line.isEmpty());

  if(Line.left(16) != "<Qucs Schematic ") {  // wrong file type ?
    file.close();
    QMessageBox::critical(0, QObject::tr("Error"),
 		 QObject::tr("Wrong document type: ")+DocName);
    return false;
  }

  Line = Line.mid(16, Line.length()-17);
  VersionTriplet DocVersion = VersionTriplet(Line);
  if (DocVersion > QucsVersion) { // wrong version number ?
    if (!QucsSettings.IgnoreFutureVersion) {
      QMessageBox::critical(0, QObject::tr("Error"),
                            QObject::tr("Wrong document version: %1").arg(DocVersion.toString()));
    }
  }

  // read content *************************
  while(!stream.atEnd()) {
    Line = stream.readLine();
    Line = Line.trimmed();
    if(Line.isEmpty()) continue;

    if(Line == "<Symbol>") {
      if(!loadPaintings(&stream, &SymbolPaints)) {
        file.close();
        return false;
      }
    }
    else
    if(Line == "<Properties>") {
      if(!loadProperties(&stream)) { file.close(); return false; } }
    else
    if(Line == "<Components>") {
      if(!loadComponents(&stream)) { file.close(); return false; } }
    else
    if(Line == "<Wires>") {
      if(!loadWires(&stream)) { file.close(); return false; } }
    else
    if(Line == "<Diagrams>") {
      if(!loadDiagrams(&stream, &DocDiags)) { file.close(); return false; } }
    else
    if(Line == "<Paintings>") {
      if(!loadPaintings(&stream, &DocPaints)) { file.close(); return false; } }
    else {
       qDebug() << Line;
       QMessageBox::critical(0, QObject::tr("Error"),
		   QObject::tr("File Format Error:\nUnknown field!"));
      file.close();
      return false;
    }
  }

  file.close();
  return true;
}

// -------------------------------------------------------------
// Creates a Qucs file format (without document properties) in the returning
// string. This is used to save state for undo operation.
QString SchematicFile::createUndoString(char Op)
{
  Wire *pw;
  Diagram *pd;
  Painting *pp;
  Component *pc;

  // Build element document.
  QString s = "  \n";
  s.replace(0,1,Op);
  for(pc = DocComps.first(); pc != 0; pc = DocComps.next()) {
    QTextStream str(&s);
    saveComponent(str, pc);
    s += "\n";
  }
  s += "</>\n";  // short end flag

  for(pw = DocWires.first(); pw != 0; pw = DocWires.next())
    s += pw->save()+"\n";
  // save all labeled nodes as wires
  for(Node *pn = DocNodes.first(); pn != 0; pn = DocNodes.next())
    if(pn->Label) s += pn->Label->save()+"\n";
  s += "</>\n";

  for(pd = DocDiags.first(); pd != 0; pd = DocDiags.next())
    s += pd->save()+"\n";
  s += "</>\n";

  for(pp = DocPaints.first(); pp != 0; pp = DocPaints.next())
    s += "<"+pp->save()+">\n";
  s += "</>\n";

  return s;
}

// -------------------------------------------------------------
// Same as "createUndoString(char Op)" but for symbol edit mode.
QString SchematicFile::createSymbolUndoString(char Op)
{
  Painting *pp;

  // Build element document.
  QString s = "  \n";
  s.replace(0,1,Op);
  s += "</>\n";  // short end flag for components
  s += "</>\n";  // short end flag for wires
  s += "</>\n";  // short end flag for diagrams

  for(pp = SymbolPaints.first(); pp != 0; pp = SymbolPaints.next())
    s += "<"+pp->save()+">\n";
  s += "</>\n";

  return s;
}

// -------------------------------------------------------------
// Is quite similiar to "loadDocument()" but with less error checking.
// Used for "undo" function.
bool SchematicFile::rebuild(QString *s)
{
  DocWires.clear();	// delete whole document
  DocNodes.clear();
  DocComps.clear();
  DocDiags.clear();
  DocPaints.clear();

  QString Line;
  QTextStream stream(s, QIODevice::ReadOnly);
  Line = stream.readLine();  // skip identity byte

  // read content *************************
  if(!loadComponents(&stream))  return false;
  if(!loadWires(&stream))  return false;
  if(!loadDiagrams(&stream, &DocDiags))  return false;
  if(!loadPaintings(&stream, &DocPaints)) return false;

  return true;
}

// -------------------------------------------------------------
// Same as "rebuild(QString *s)" but for symbol edit mode.
bool SchematicFile::rebuildSymbol(QString *s)
{
  SymbolPaints.clear();	// delete whole document

  QString Line;
  QTextStream stream(s, QIODevice::ReadOnly);
  Line = stream.readLine();  // skip identity byte

  // read content *************************
  Line = stream.readLine();  // skip components
  Line = stream.readLine();  // skip wires
  Line = stream.readLine();  // skip diagrams
  if(!loadPaintings(&stream, &SymbolPaints)) return false;

  return true;
}


// ***************************************************************
// *****                                                     *****
// *****             Functions to create netlist             *****
// *****                                                     *****
// ***************************************************************

void SchematicFile::createNodeSet(QStringList& Collect, int& countInit,
			      Conductor *pw, Node *p1)
{
  if(pw->Label)
    if(!pw->Label->initValue.isEmpty())
      Collect.append("NodeSet:NS" + QString::number(countInit++) + " " +
                     p1->Name + " U=\"" + pw->Label->initValue + "\"");
}

// ---------------------------------------------------
void SchematicFile::throughAllNodes(bool User, QStringList& Collect,
				int& countInit)
{
  Node *pn;
  int z=0;

  for(pn = scene->DocNodes.first(); pn != 0; pn = scene->DocNodes.next()) {
    if(pn->Name.isEmpty() == User) {
      continue;  // already named ?
    }
    if(!User) {
      if(isAnalog)
	pn->Name = "_net";
      else
	pn->Name = "net_net";   // VHDL names must not begin with '_'
      pn->Name += QString::number(z++);  // create numbered node name
    }
    else if(pn->State) {
      continue;  // already worked on
    }

    if(isAnalog) createNodeSet(Collect, countInit, pn, pn);

    pn->State = 1;
    propagateNode(Collect, countInit, pn);
  }
}

// ----------------------------------------------------------
// Checks whether this file is a qucs file and whether it is an subcircuit.
// It returns the number of subcircuit ports.
int SchematicFile::testFile(const QString& DocName)
{
  QFile file(DocName);
  if(!file.open(QIODevice::ReadOnly)) {
    return -1;
  }

  QString Line;
  // .........................................
  // To strongly speed up the file read operation the whole file is
  // read into the memory in one piece.
  QTextStream ReadWhole(&file);
  QString FileString = ReadWhole.readAll();
  file.close();
  QTextStream stream(&FileString, QIODevice::ReadOnly);


  // read header ........................
  do {
    if(stream.atEnd()) {
      file.close();
      return -2;
    }
    Line = stream.readLine();
    Line = Line.trimmed();
  } while(Line.isEmpty());

  if(Line.left(16) != "<Qucs Schematic ") {  // wrong file type ?
    file.close();
    return -3;
  }

  Line = Line.mid(16, Line.length()-17);
  VersionTriplet DocVersion = VersionTriplet(Line);
  if (DocVersion > QucsVersion) { // wrong version number ?
      if (!QucsSettings.IgnoreFutureVersion) {
          file.close();
          return -4;
      }
    //file.close();
    //return -4;
  }

  // read content ....................
  while(!stream.atEnd()) {
    Line = stream.readLine();
    if(Line == "<Components>") break;
  }

  int z=0;
  while(!stream.atEnd()) {
    Line = stream.readLine();
    if(Line == "</Components>") {
      file.close();
      return z;       // return number of ports
    }

    Line = Line.trimmed();
    QString s = Line.section(' ',0,0);    // component type
    if(s == "<Port") z++;
  }
  return -5;  // component field not closed
}

// ---------------------------------------------------
// Collects the signal names for digital simulations.
void SchematicFile::collectDigitalSignals(void)
{
  Node *pn;

  for(pn = DocNodes.first(); pn != 0; pn = DocNodes.next()) {
    DigMap::Iterator it = Signals.find(pn->Name);
    if(it == Signals.end()) { // avoid redeclaration of signal
      Signals.insert(pn->Name, DigSignal(pn->Name, pn->DType));
    } else if (!pn->DType.isEmpty()) {
      it.value().Type = pn->DType;
    }
  }
}

// ---------------------------------------------------
// Propagates the given node to connected component ports.
void SchematicFile::propagateNode(QStringList& Collect,
			      int& countInit, Node *pn)
{
  bool setName=false;
  Q3PtrList<Node> Cons;
  Node *p2;
  Wire *pw;
  Element *pe;

  Cons.append(pn);
  for(p2 = Cons.first(); p2 != 0; p2 = Cons.next())
    for(pe = p2->Connections.first(); pe != 0; pe = p2->Connections.next())
      if(pe->ElemType == isWire) {
	pw = (Wire*)pe;
	if(p2 != pw->Port1) {
	  if(pw->Port1->Name.isEmpty()) {
	    pw->Port1->Name = pn->Name;
	    pw->Port1->State = 1;
	    Cons.append(pw->Port1);
	    setName = true;
	  }
	}
	else {
	  if(pw->Port2->Name.isEmpty()) {
	    pw->Port2->Name = pn->Name;
	    pw->Port2->State = 1;
	    Cons.append(pw->Port2);
	    setName = true;
	  }
	}
	if(setName) {
	  Cons.findRef(p2);   // back to current Connection
	  if (isAnalog) createNodeSet(Collect, countInit, pw, pn);
	  setName = false;
	}
      }
  Cons.clear();
}

#include <iostream>

/*!
 * \brief Schematic::throughAllComps
 * Goes through all schematic components and allows special component
 * handling, e.g. like subcircuit netlisting.
 * \param stream is a pointer to the text stream used to collect the netlist
 * \param countInit is the reference to a counter for nodesets (initial conditions)
 * \param Collect is the reference to a list of collected nodesets
 * \param ErrText is pointer to the QPlainTextEdit used for error messages
 * \param NumPorts counter for the number of ports
 * \return true in case of success (false otherwise)
 */
bool SchematicFile::throughAllComps(QTextStream *stream, int& countInit,
                   QStringList& Collect, QPlainTextEdit *ErrText, int NumPorts)
{
  bool r;
  QString s;

  // give the ground nodes the name "gnd", and insert subcircuits etc.
  for(auto it=scene->DocComps.begin(); it!=scene->DocComps.end(); ++it) {
    Component *pc=*it;

    if(pc->isActive != COMP_IS_ACTIVE) continue;

    // check analog/digital typed components
    if(isAnalog) {
      if((pc->type() & isAnalogComponent) == 0) {
        ErrText->appendPlainText(QObject::tr("ERROR: Component \"%1\" has no analog model.").arg(pc->name()));
        return false;
      }
    } else {
      if((pc->type() & isDigitalComponent) == 0) {
        ErrText->appendPlainText(QObject::tr("ERROR: Component \"%1\" has no digital model.").arg(pc->name()));
        return false;
      }
    }

    // handle ground symbol
    if(pc->obsolete_model_hack() == "GND") { // BUG.
      pc->Ports.first()->Connection->Name = "gnd";
      continue;
    }

    // handle subcircuits
    if(pc->obsolete_model_hack() == "Sub")
    {
      int i;
      // tell the subcircuit it belongs to this schematic
      pc->setSchematic (scene);
      QString f = pc->getSubcircuitFile();
      SubMap::Iterator it = FileList.find(f);
      if(it != FileList.end())
      {
        if (!it.value().PortTypes.isEmpty())
        {
          i = 0;
          // apply in/out signal types of subcircuit
          foreach(Port *pp, pc->Ports)
          {
            pp->Type = it.value().PortTypes[i];
            pp->Connection->DType = pp->Type;
            i++;
          }
        }
        continue;   // insert each subcircuit just one time
      }

      // The subcircuit has not previously been added
      SubFile sub = SubFile("SCH", f);
      FileList.insert(f, sub);


      // load subcircuit schematic
      s = pc->Props.first()->Value;
      // legacy created a SchematicView->QucsDoc and set DocName
      //SchematicFile *d = new SchematicFile(0, pc->getSubcircuitFile());
      SchematicFile *d = new SchematicFile();
      d->DocName = pc->getSubcircuitFile();
      if(!d->loadDocument())      // load document if possible
      {
          delete d;
          /// \todo implement error/warning message dispatcher for GUI and CLI modes.
          QString message = QObject::tr("ERROR: Cannot load subcircuit \"%1\".").arg(s);
          if (QucsMain) // GUI is running
            ErrText->appendPlainText(message);
          else // command line
            qCritical() << "Schematic::throughAllComps" << message;
          return false;
      }
      d->DocName = s;
      d->isVerilog = isVerilog;
      d->isAnalog = isAnalog;
      d->creatingLib = creatingLib;
      r = d->createSubNetlist(stream, countInit, Collect, ErrText, NumPorts);
      if (r)
      {
        i = 0;
        // save in/out signal types of subcircuit
        foreach(Port *pp, pc->Ports)
        {
            //if(i>=d->PortTypes.count())break;
            pp->Type = d->PortTypes[i];
            pp->Connection->DType = pp->Type;
            i++;
        }
        sub.PortTypes = d->PortTypes;
        FileList.insert(f, sub);
      }
      delete d;
      if(!r)
      {
        return false;
      }
      continue;
    } // if(pc->Model == "Sub")

    if(LibComp* lib = dynamic_cast</*const*/LibComp*>(pc)) {
      if(creatingLib) {
	ErrText->appendPlainText(
	    QObject::tr("WARNING: Skipping library component \"%1\".").
	    arg(pc->name()));
	continue;
      }
      QString scfile = pc->getSubcircuitFile();
      s = scfile + "/" + pc->Props.at(1)->Value;
      SubMap::Iterator it = FileList.find(s);
      if(it != FileList.end())
        continue;   // insert each library subcircuit just one time
      FileList.insert(s, SubFile("LIB", s));


      //FIXME: use different netlister for different purposes
      unsigned whatisit = isAnalog?1:(isVerilog?4:2);
      r = lib->createSubNetlist(stream, Collect, whatisit);
      if(!r) {
	ErrText->appendPlainText(
	    QObject::tr("ERROR: \"%1\": Cannot load library component \"%2\" from \"%3\"").
	    arg(pc->name(), pc->Props.at(1)->Value, scfile));
	return false;
      }
      continue;
    }

    // handle SPICE subcircuit components
    if(pc->obsolete_model_hack() == "SPICE") { // BUG
      s = pc->Props.first()->Value;
      // tell the spice component it belongs to this schematic
      pc->setSchematic (scene);
      if(s.isEmpty()) {
        ErrText->appendPlainText(QObject::tr("ERROR: No file name in SPICE component \"%1\".").
                        arg(pc->name()));
        return false;
      }
      QString f = pc->getSubcircuitFile();
      SubMap::Iterator it = FileList.find(f);
      if(it != FileList.end())
        continue;   // insert each spice component just one time
      FileList.insert(f, SubFile("CIR", f));

      SpiceFile *sf = (SpiceFile*)pc;
      r = sf->createSubNetlist(stream);
      ErrText->appendPlainText(sf->getErrorText());
      if(!r){
        return false;
      }
      continue;
    }

    // handle digital file subcircuits
    if(pc->obsolete_model_hack() == "VHDL" || pc->obsolete_model_hack() == "Verilog") {
      if(isVerilog && pc->obsolete_model_hack() == "VHDL")
	continue;
      if(!isVerilog && pc->obsolete_model_hack() == "Verilog")
	continue;
      s = pc->Props.getFirst()->Value;
      if(s.isEmpty()) {
        ErrText->appendPlainText(QObject::tr("ERROR: No file name in %1 component \"%2\".").
			arg(pc->obsolete_model_hack()).
                        arg(pc->name()));
        return false;
      }
      QString f = pc->getSubcircuitFile();
      SubMap::Iterator it = FileList.find(f);
      if(it != FileList.end())
        continue;   // insert each vhdl/verilog component just one time
      s = ((pc->obsolete_model_hack() == "VHDL") ? "VHD" : "VER");
      FileList.insert(f, SubFile(s, f));

      if(pc->obsolete_model_hack() == "VHDL") {
	VHDL_File *vf = (VHDL_File*)pc;
	r = vf->createSubNetlist(stream);
	ErrText->appendPlainText(vf->getErrorText());
	if(!r) {
	  return false;
	}
      }
      if(pc->obsolete_model_hack() == "Verilog") {
	Verilog_File *vf = (Verilog_File*)pc;
	r = vf->createSubNetlist(stream);
	ErrText->appendPlainText(vf->getErrorText());
	if(!r) {
	  return false;
	}
      }
      continue;
    }
  }
  return true;
}

// ---------------------------------------------------
// Follows the wire lines in order to determine the node names for
// each component. Output into "stream", NodeSets are collected in
// "Collect" and counted with "countInit".
bool SchematicFile::giveNodeNames(QTextStream *stream, int& countInit,
                   QStringList& Collect, QPlainTextEdit *ErrText, int NumPorts)
{
  // delete the node names
  for(Node *pn = scene->DocNodes.first(); pn != 0; pn = scene->DocNodes.next()) {
    pn->State = 0;
    if(pn->Label) {
      if(isAnalog)
        pn->Name = pn->Label->Name;
      else
        pn->Name = "net" + pn->Label->Name;
    }
    else pn->Name = "";
  }

  // set the wire names to the connected node
  for(Wire *pw = scene->DocWires.first(); pw != 0; pw = scene->DocWires.next())
    if(pw->Label != 0) {
      if(isAnalog)
        pw->Port1->Name = pw->Label->Name;
      else  // avoid to use reserved VHDL words
        pw->Port1->Name = "net" + pw->Label->Name;
    }

  // go through components
  if(!throughAllComps(stream, countInit, Collect, ErrText, NumPorts)){
    fprintf(stderr, "Error: Could not go throughAllComps\n");
    return false;
  }

  // work on named nodes first in order to preserve the user given names
  throughAllNodes(true, Collect, countInit);

  // give names to the remaining (unnamed) nodes
  throughAllNodes(false, Collect, countInit);

  if(!isAnalog) // collect all node names for VHDL signal declaration
    collectDigitalSignals();

  return true;
}

// ---------------------------------------------------
bool SchematicFile::createLibNetlist(QTextStream *stream, QPlainTextEdit *ErrText,
				 int NumPorts)
{
  int countInit = 0;
  QStringList Collect;
  Collect.clear();
  FileList.clear();
  Signals.clear();
  // Apply node names and collect subcircuits and file include
  creatingLib = true;
  if(!giveNodeNames(stream, countInit, Collect, ErrText, NumPorts)) {
    creatingLib = false;
    return false;
  }
  creatingLib = false;

  // Marking start of actual top-level subcircuit
  QString c;
  if(!isAnalog) {
    if (isVerilog)
      c = "///";
    else
      c = "---";
  }
  else c = "###";
  (*stream) << "\n" << c << " TOP LEVEL MARK " << c << "\n";

  // Emit subcircuit components
  createSubNetlistPlain(stream, ErrText, NumPorts);

  Signals.clear();  // was filled in "giveNodeNames()"
  return true;
}

//#define VHDL_SIGNAL_TYPE "bit"
//#define VHDL_LIBRARIES   ""
#define VHDL_SIGNAL_TYPE "std_logic"
#define VHDL_LIBRARIES   "\nlibrary ieee;\nuse ieee.std_logic_1164.all;\n"

// ---------------------------------------------------
void SchematicFile::createSubNetlistPlain(QTextStream *stream, QPlainTextEdit *ErrText,
int NumPorts)
{
  int i, z;
  QString s;
  QStringList SubcircuitPortNames;
  QStringList SubcircuitPortTypes;
  QStringList InPorts;
  QStringList OutPorts;
  QStringList InOutPorts;
  QStringList::iterator it_name;
  QStringList::iterator it_type;
  Component *pc;

  // probably creating a library currently
  QTextStream * tstream = stream;
  QFile ofile;
  if(creatingLib) {
    QString f = misc::properAbsFileName(DocName) + ".lst";
    ofile.setFileName(f);
    if(!ofile.open(QIODevice::WriteOnly)) {
      ErrText->appendPlainText(tr("ERROR: Cannot create library file \"%s\".").arg(f));
      return;
    }
    tstream = new QTextStream(&ofile);
  }

  // collect subcircuit ports and sort their node names into
  // "SubcircuitPortNames"
  PortTypes.clear();
  for(pc = DocComps.first(); pc != 0; pc = DocComps.next()) {
    if(pc->obsolete_model_hack().at(0) == '.') { // no simulations in subcircuits
      ErrText->appendPlainText(
        QObject::tr("WARNING: Ignore simulation component in subcircuit \"%1\".").arg(DocName)+"\n");
      continue;
    }
    else if(pc->obsolete_model_hack() == "Port") {
      i = pc->Props.first()->Value.toInt();
      for(z=SubcircuitPortNames.size(); z<i; z++) { // add empty port names
        SubcircuitPortNames.append(" ");
        SubcircuitPortTypes.append(" ");
      }
      it_name = SubcircuitPortNames.begin();
      it_type = SubcircuitPortTypes.begin();
      for(int n=1;n<i;n++)
      {
        it_name++;
        it_type++;
      }
      (*it_name) = pc->Ports.first()->Connection->Name;
      DigMap::Iterator it = Signals.find(*it_name);
      if(it!=Signals.end())
        (*it_type) = it.value().Type;
      // propagate type to port symbol
      pc->Ports.first()->Connection->DType = *it_type;

      if(!isAnalog) {
        if (isVerilog) {
          Signals.remove(*it_name); // remove node name
          switch(pc->Props.at(1)->Value.at(0).toLatin1()) {
            case 'a':
              InOutPorts.append(*it_name);
              break;
            case 'o':
              OutPorts.append(*it_name);
              break;
              default:
                InPorts.append(*it_name);
          }
        }
        else {
          // remove node name of output port
          Signals.remove(*it_name);
          switch(pc->Props.at(1)->Value.at(0).toLatin1()) {
            case 'a':
              (*it_name) += " : inout"; // attribute "analog" is "inout"
              break;
            case 'o': // output ports need workaround
              Signals.insert(*it_name, DigSignal(*it_name, *it_type));
              (*it_name) = "net_out" + (*it_name);
              // fall through
            default:
              (*it_name) += " : " + pc->Props.at(1)->Value;
          }
          (*it_name) += " " + ((*it_type).isEmpty() ?
          VHDL_SIGNAL_TYPE : (*it_type));
        }
      }
    }
  }

  // remove empty subcircuit ports (missing port numbers)
  for(it_name = SubcircuitPortNames.begin(),
      it_type = SubcircuitPortTypes.begin();
      it_name != SubcircuitPortNames.end(); ) {
    if(*it_name == " ") {
      it_name = SubcircuitPortNames.erase(it_name);
      it_type = SubcircuitPortTypes.erase(it_type);
    } else {
      PortTypes.append(*it_type);
      it_name++;
      it_type++;
    }
  }

  QString f = misc::properFileName(DocName);
  QString Type = misc::properName(f);

  Painting *pi;
  if(isAnalog) {
    // ..... analog subcircuit ...................................
    (*tstream) << "\n.Def:" << Type << " " << SubcircuitPortNames.join(" ");
    for(pi = SymbolPaints.first(); pi != 0; pi = SymbolPaints.next())
      if(pi->Name == ".ID ") {
        ID_Text *pid = (ID_Text*)pi;
        QList<SubParameter *>::const_iterator it;
        for(it = pid->Parameter.constBegin(); it != pid->Parameter.constEnd(); it++) {
          s = (*it)->Name; // keep 'Name' unchanged
          (*tstream) << " " << s.replace("=", "=\"") << '"';
        }
        break;
      }
    (*tstream) << '\n';

    // write all components with node names into netlist file
    for(pc = DocComps.first(); pc != 0; pc = DocComps.next())
      (*tstream) << pc->getNetlist();

    (*tstream) << ".Def:End\n";

  }
  else {
    if (isVerilog) {
      // ..... digital subcircuit ...................................
      (*tstream) << "\nmodule Sub_" << Type << " ("
              << SubcircuitPortNames.join(", ") << ");\n";

      // subcircuit in/out connections
      if(!InPorts.isEmpty())
        (*tstream) << " input " << InPorts.join(", ") << ";\n";
      if(!OutPorts.isEmpty())
        (*tstream) << " output " << OutPorts.join(", ") << ";\n";
      if(!InOutPorts.isEmpty())
        (*tstream) << " inout " << InOutPorts.join(", ") << ";\n";

      // subcircuit connections
      if(!Signals.isEmpty()) {
        QList<DigSignal> values = Signals.values();
        QList<DigSignal>::const_iterator it;
        for (it = values.constBegin(); it != values.constEnd(); ++it) {
          (*tstream) << " wire " << (*it).Name << ";\n";
        }
      }
      (*tstream) << "\n";

      // subcircuit parameters
      for(pi = SymbolPaints.first(); pi != 0; pi = SymbolPaints.next())
        if(pi->Name == ".ID ") {
          QList<SubParameter *>::const_iterator it;
          ID_Text *pid = (ID_Text*)pi;
          for(it = pid->Parameter.constBegin(); it != pid->Parameter.constEnd(); it++) {
            s = (*it)->Name.section('=', 0,0);
            QString v = misc::Verilog_Param((*it)->Name.section('=', 1,1));
            (*tstream) << " parameter " << s << " = " << v << ";\n";
          }
          (*tstream) << "\n";
          break;
        }

      // write all equations into netlist file
      for(pc = DocComps.first(); pc != 0; pc = DocComps.next()) {
        if(pc->obsolete_model_hack() == "Eqn") {
          (*tstream) << pc->get_Verilog_Code(NumPorts);
        }
      }

      if(Signals.find("gnd") != Signals.end())
      (*tstream) << " assign gnd = 0;\n"; // should appear only once

      // write all components into netlist file
      for(pc = DocComps.first(); pc != 0; pc = DocComps.next()) {
        if(pc->obsolete_model_hack() != "Eqn") {
          s = pc->get_Verilog_Code(NumPorts);
          if(s.length()>0 && s.at(0) == '\xA7') {  //section symbol
            ErrText->appendPlainText(s.mid(1));
          }
          else (*tstream) << s;
        }
      }

      (*tstream) << "endmodule\n";
    } else {
      // ..... digital subcircuit ...................................
      (*tstream) << VHDL_LIBRARIES;
      (*tstream) << "entity Sub_" << Type << " is\n"
                << " port ("
                << SubcircuitPortNames.join(";\n ") << ");\n";

      for(pi = SymbolPaints.first(); pi != 0; pi = SymbolPaints.next())
        if(pi->Name == ".ID ") {
          ID_Text *pid = (ID_Text*)pi;
          QList<SubParameter *>::const_iterator it;

          if (pid->Parameter.size()) {
            (*tstream) << " generic (";
            for(it = pid->Parameter.constBegin(); it != pid->Parameter.constEnd(); it++) {
              s = (*it)->Name;
              QString t = (*it)->Type.isEmpty() ? "real" : (*it)->Type;
              (*tstream) << s.replace("=", " : "+t+" := ") << ";\n ";
            }
            (*tstream) << ");\n";
          }
          break;
        }

      (*tstream) << "end entity;\n"
                  << "use work.all;\n"
                  << "architecture Arch_Sub_" << Type << " of Sub_" << Type
                  << " is\n";

      if(!Signals.isEmpty()) {
        QList<DigSignal> values = Signals.values();
        QList<DigSignal>::const_iterator it;
        for (it = values.constBegin(); it != values.constEnd(); ++it) {
          (*tstream) << " signal " << (*it).Name << " : "
          << ((*it).Type.isEmpty() ?
          VHDL_SIGNAL_TYPE : (*it).Type) << ";\n";
        }
      }

      // write all equations into netlist file
      for(pc = DocComps.first(); pc != 0; pc = DocComps.next()) {
        if(pc->obsolete_model_hack() == "Eqn") {
          ErrText->appendPlainText(
                      QObject::tr("WARNING: Equations in \"%1\" are 'time' typed.").
          arg(pc->name()));
          (*tstream) << pc->get_VHDL_Code(NumPorts);
        }
      }

      (*tstream) << "begin\n";

      if(Signals.find("gnd") != Signals.end())
      (*tstream) << " gnd <= '0';\n"; // should appear only once

      // write all components into netlist file
      for(pc = DocComps.first(); pc != 0; pc = DocComps.next()) {
        if(pc->obsolete_model_hack() != "Eqn") {
            s = pc->get_VHDL_Code(NumPorts);
            if(s.length()>0 && s.at(0) == '\xA7') {  //section symbol
              ErrText->appendPlainText(s.mid(1));
          }
          else (*tstream) << s;
        }
      }

      (*tstream) << "end architecture;\n";
    }
  }

  // close file
  if(creatingLib) {
    ofile.close();
    delete tstream;
  }
}
// ---------------------------------------------------
// Write the netlist as subcircuit to the text stream 'stream'.
bool SchematicFile::createSubNetlist(QTextStream *stream, int& countInit,
                     QStringList& Collect, QPlainTextEdit *ErrText, int NumPorts)
{
//  int Collect_count = Collect.count();   // position for this subcircuit

  // TODO: NodeSets have to be put into the subcircuit block.
  if(!giveNodeNames(stream, countInit, Collect, ErrText, NumPorts)){
    fprintf(stderr, "Error giving NodeNames in createSubNetlist\n");
    return false;
  }

/*  Example for TODO
      for(it = Collect.at(Collect_count); it != Collect.end(); )
      if((*it).left(4) == "use ") {  // output all subcircuit uses
        (*stream) << (*it);
        it = Collect.remove(it);
      }
      else it++;*/

  // Emit subcircuit components
  createSubNetlistPlain(stream, ErrText, NumPorts);

  Signals.clear();  // was filled in "giveNodeNames()"
  return true;
}

// ---------------------------------------------------
// Determines the node names and writes subcircuits into netlist file.
int SchematicFile::prepareNetlist(QTextStream& stream, QStringList& Collect,
                              QPlainTextEdit *ErrText)
{
  if(scene->showBias > 0) {
      scene->showBias = -1;  // do not show DC bias anymore
  }

  isVerilog = false;
  isAnalog = true;
  bool isTruthTable = false;
  int allTypes = 0, NumPorts = 0;

  // Detect simulation domain (analog/digital) by looking at component types.
  //for(Component *pc = DocComps.first(); pc != 0; pc = DocComps.next()) {
  for(Component *pc = scene->DocComps.first(); pc != 0; pc = scene->DocComps.next()) {
    if(pc->isActive == COMP_IS_OPEN) continue;
    if(pc->obsolete_model_hack().at(0) == '.') {
      if(pc->obsolete_model_hack() == ".Digi") {
        if(allTypes & isDigitalComponent) {
          ErrText->appendPlainText(
             QObject::tr("ERROR: Only one digital simulation allowed."));
          return -10;
        }
        if(pc->Props.getFirst()->Value != "TimeList")
          isTruthTable = true;
	      if(pc->Props.getLast()->Value != "VHDL")
	        isVerilog = true;
        allTypes |= isDigitalComponent;
	      isAnalog = false;
      }
      else allTypes |= isAnalogComponent;
      if((allTypes & isComponent) == isComponent) {
        ErrText->appendPlainText(
           QObject::tr("ERROR: Analog and digital simulations cannot be mixed."));
        return -10;
      }
    }
    else if(pc->obsolete_model_hack() == "DigiSource") NumPorts++;
  }

  if((allTypes & isAnalogComponent) == 0) {
    if(allTypes == 0) {
      // If no simulation exists, assume analog simulation. There may
      // be a simulation within a SPICE file. Otherwise Qucsator will
      // output an error.
      isAnalog = true;
      allTypes |= isAnalogComponent;
      NumPorts = -1;
    }
    else {
      if(NumPorts < 1 && isTruthTable) {
        ErrText->appendPlainText(
           QObject::tr("ERROR: Digital simulation needs at least one digital source."));
        return -10;
      }
      if(!isTruthTable) NumPorts = 0;
    }
  }
  else {
    NumPorts = -1;
    isAnalog = true;
  }

  // first line is documentation
  if(allTypes & isAnalogComponent)
    stream << "#";
  else if (isVerilog)
    stream << "//";
  else
    stream << "--";
  stream << " Qucs " << PACKAGE_VERSION << "  " << DocName << "\n";

  // set timescale property for verilog schematics
  if (isVerilog) {
    stream << "\n`timescale 1ps/100fs\n";
  }

  int countInit = 0;  // counts the nodesets to give them unique names
  if(!giveNodeNames(&stream, countInit, Collect, ErrText, NumPorts)){
    fprintf(stderr, "Error giving NodeNames\n");
    return -10;
  }

  if(allTypes & isAnalogComponent){
    return NumPorts;
  }

  if (!isVerilog) {
    stream << VHDL_LIBRARIES;
    stream << "entity TestBench is\n"
	   << "end entity;\n"
	   << "use work.all;\n";
  }
  return NumPorts;
}

// ---------------------------------------------------
// Write the beginning of digital netlist to the text stream 'stream'.
void SchematicFile::beginNetlistDigital(QTextStream& stream)
{
  if (isVerilog) {
    stream << "module TestBench ();\n";
    QList<DigSignal> values = Signals.values();
    QList<DigSignal>::const_iterator it;
    for (it = values.constBegin(); it != values.constEnd(); ++it) {
      stream << "  wire " << (*it).Name << ";\n";
    }
    stream << "\n";
  } else {
    stream << "architecture Arch_TestBench of TestBench is\n";
    QList<DigSignal> values = Signals.values();
    QList<DigSignal>::const_iterator it;
    for (it = values.constBegin(); it != values.constEnd(); ++it) {
      stream << "  signal " << (*it).Name << " : "
	     << ((*it).Type.isEmpty() ?
		 VHDL_SIGNAL_TYPE : (*it).Type) << ";\n";
    }
    stream << "begin\n";
  }

  if(Signals.find("gnd") != Signals.end()) {
    if (isVerilog) {
      stream << "  assign gnd = 0;\n";
    } else {
      stream << "  gnd <= '0';\n";  // should appear only once
    }
  }
}

// ---------------------------------------------------
// Write the end of digital netlist to the text stream 'stream'.
void SchematicFile::endNetlistDigital(QTextStream& stream)
{
  if (isVerilog) {
  } else {
    stream << "end architecture;\n";
  }
}

// ---------------------------------------------------
// write all components with node names into the netlist file
QString SchematicFile::createNetlist(QTextStream& stream, int NumPorts)
{
  if(!isAnalog) {
    beginNetlistDigital(stream);
  }

  Signals.clear();  // was filled in "giveNodeNames()"
  FileList.clear();

  QString s, Time;
  for(Component *pc = scene->DocComps.first(); pc != 0; pc = scene->DocComps.next()) {
    if(isAnalog) {
      s = pc->getNetlist();
    }
    else {
      if(pc->obsolete_model_hack() == ".Digi" && pc->isActive) {  // simulation component ?
        if(NumPorts > 0) { // truth table simulation ?
	  if (isVerilog)
	    Time = QString::number((1 << NumPorts));
	  else
	    Time = QString::number((1 << NumPorts) - 1) + " ns";
        } else {
          Time = pc->Props.at(1)->Value;
	  if (isVerilog) {
	    if(!misc::Verilog_Time(Time, pc->name())) return Time;
	  } else {
	    if(!misc::VHDL_Time(Time, pc->name())) return Time;  // wrong time format
	  }
        }
      }
      if (isVerilog) {
	s = pc->get_Verilog_Code(NumPorts);
      } else {
	s = pc->get_VHDL_Code(NumPorts);
      }
      if (s.length()>0 && s.at(0) == '\xA7'){
          return s; // return error
      }
    }
    stream << s;
  }

  if(!isAnalog) {
    endNetlistDigital(stream);
  }

  return Time;
}

// Copy function,
void SchematicFile::copy()
{
  QString s = createClipboardFile();
  QClipboard *cb = QApplication::clipboard();  // get system clipboard
  if (!s.isEmpty()) {
    cb->setText(s, QClipboard::Clipboard);
  }
}

// ---------------------------------------------------
// Cut function, copy followed by deletion
void SchematicFile::cut()
{
  copy();
  scene->deleteElements(); //delete selected elements
  scene->update();
}

// ---------------------------------------------------
// Performs paste function from clipboard
bool SchematicFile::paste(QTextStream *stream, Q3PtrList<Element> *pe)
{
  return pasteFromClipboard(stream, pe);
}

// ---------------------------------------------------
// Loads this Qucs document.
bool SchematicFile::load()
{
  DocComps.clear();
  DocWires.clear();
  DocNodes.clear();
  DocDiags.clear();
  DocPaints.clear();
  SymbolPaints.clear();

  if(!loadDocument()) return false;
  lastSaved = QDateTime::currentDateTime();

  TODO("check legacy load code");
  // have to call this to avoid crash at sizeOfAll
  //?becomeCurrent(false);

  //?sizeOfAll(UsedX1, UsedY1, UsedX2, UsedY2);
  if(ViewX1 > UsedX1)  ViewX1 = UsedX1;
  if(ViewY1 > UsedY1)  ViewY1 = UsedY1;
  if(ViewX2 < UsedX2)  ViewX2 = UsedX2;
  if(ViewY2 < UsedY2)  ViewY2 = UsedY2;
  //?zoomReset();
  TODO("Fix setContentsPos");
  /// \todo setContentsPos(tmpViewX1, tmpViewY1);
  tmpViewX1 = tmpViewY1 = -200;   // was used as temporary cache
  return true;
}

// ---------------------------------------------------
// Saves this Qucs document. Returns the number of subcircuit ports.
int SchematicFile::save()
{
  int result = adjustPortNumbers();// same port number for schematic and symbol
  if(saveDocument() < 0)
     return -1;

  QFileInfo Info(DocName);
  lastSaved = Info.lastModified();

  // update the subcircuit file lookup hashes
  QucsMain->updateSchNameHash();
  QucsMain->updateSpiceNameHash();

  return result;
}

// ---------------------------------------------------
// If the port number of the schematic and of the symbol are not
// equal add or remove some in the symbol.
int SchematicFile::adjustPortNumbers()
{
  TODO("fix legacy code");
  int x1, x2, y1, y2;
  /* ??
  // get size of whole symbol to know where to place new ports
  if(symbolMode)  sizeOfAll(x1, y1, x2, y2);
  else {
    Components = &SymbolComps;
    Wires      = &SymbolWires;
    Nodes      = &SymbolNodes;
    Diagrams   = &SymbolDiags;
    Paintings  = &SymbolPaints;
    sizeOfAll(x1, y1, x2, y2);
    Components = &DocComps;
    Wires      = &DocWires;
    Nodes      = &DocNodes;
    Diagrams   = &DocDiags;
    Paintings  = &DocPaints;
  }
  x1 += 40;
  y2 += 20;
  setOnGrid(x1, y2);
  */

  Painting *pp;
  // delete all port names in symbol
  for(pp = SymbolPaints.first(); pp!=0; pp = SymbolPaints.next())
    if(pp->Name == ".PortSym ")
      ((PortSymbol*)pp)->nameStr = "";

  QString Str;
  int countPort = 0;

  QFileInfo Info (DataDisplay);
  QString Suffix = Info.suffix();

  // handle VHDL file symbol
  if (Suffix == "vhd" || Suffix == "vhdl") {
    QStringList::iterator it;
    QStringList Names, GNames, GTypes, GDefs;
    int Number;

    // get ports from VHDL file
    QFileInfo Info(DocName);
    QString Name = Info.path() + QDir::separator() + DataDisplay;

    // obtain VHDL information either from open text document or the
    // file directly
    VHDL_File_Info VInfo;
    TextDoc * d = (TextDoc*)QucsMain->findDoc (Name);
    if (d)
      VInfo = VHDL_File_Info (d->document()->toPlainText());
    else
      VInfo = VHDL_File_Info (Name, true);

    if (!VInfo.PortNames.isEmpty())
      Names = VInfo.PortNames.split(",", QString::SkipEmptyParts);

    for(pp = SymbolPaints.first(); pp!=0; pp = SymbolPaints.next())
      if(pp->Name == ".ID ") {
	ID_Text * id = (ID_Text *) pp;
	id->Prefix = VInfo.EntityName.toUpper();
	id->Parameter.clear();
	if (!VInfo.GenNames.isEmpty())
	  GNames = VInfo.GenNames.split(",", QString::SkipEmptyParts);
	if (!VInfo.GenTypes.isEmpty())
	  GTypes = VInfo.GenTypes.split(",", QString::SkipEmptyParts);
	if (!VInfo.GenDefs.isEmpty())
	  GDefs = VInfo.GenDefs.split(",", QString::SkipEmptyParts);;
	for(Number = 1, it = GNames.begin(); it != GNames.end(); ++it) {
	  id->Parameter.append(new SubParameter(
 	    true,
	    *it+"="+GDefs[Number-1],
	    tr("generic")+" "+QString::number(Number),
	    GTypes[Number-1]));
	  Number++;
	}
      }

    for(Number = 1, it = Names.begin(); it != Names.end(); ++it, Number++) {
      countPort++;

      Str = QString::number(Number);
      // search for matching port symbol
      for(pp = SymbolPaints.first(); pp!=0; pp = SymbolPaints.next())
	if(pp->Name == ".PortSym ")
	  if(((PortSymbol*)pp)->numberStr == Str) break;

      if(pp)
	((PortSymbol*)pp)->nameStr = *it;
      else {
	SymbolPaints.append(new PortSymbol(x1, y2, Str, *it));
	y2 += 40;
      }
    }
  }
  // handle Verilog-HDL file symbol
  else if (Suffix == "v") {

    QStringList::iterator it;
    QStringList Names;
    int Number;

    // get ports from Verilog-HDL file
    QFileInfo Info (DocName);
    QString Name = Info.path() + QDir::separator() + DataDisplay;

    // obtain Verilog-HDL information either from open text document or the
    // file directly
    Verilog_File_Info VInfo;
    TextDoc * d = (TextDoc*)QucsMain->findDoc (Name);
    if (d)
      VInfo = Verilog_File_Info (d->document()->toPlainText());
    else
      VInfo = Verilog_File_Info (Name, true);
    if (!VInfo.PortNames.isEmpty())
      Names = VInfo.PortNames.split(",", QString::SkipEmptyParts);

    for(pp = SymbolPaints.first(); pp!=0; pp = SymbolPaints.next())
      if(pp->Name == ".ID ") {
	ID_Text * id = (ID_Text *) pp;
	id->Prefix = VInfo.ModuleName.toUpper();
	id->Parameter.clear();
      }

    for(Number = 1, it = Names.begin(); it != Names.end(); ++it, Number++) {
      countPort++;

      Str = QString::number(Number);
      // search for matching port symbol
      for(pp = SymbolPaints.first(); pp!=0; pp = SymbolPaints.next())
	if(pp->Name == ".PortSym ")
	  if(((PortSymbol*)pp)->numberStr == Str) break;

      if(pp)
	((PortSymbol*)pp)->nameStr = *it;
      else {
	SymbolPaints.append(new PortSymbol(x1, y2, Str, *it));
	y2 += 40;
      }
    }
  }
  // handle Verilog-A file symbol
  else if (Suffix == "va") {

    QStringList::iterator it;
    QStringList Names;
    int Number;

    // get ports from Verilog-A file
    QFileInfo Info (DocName);
    QString Name = Info.path() + QDir::separator() + DataDisplay;

    // obtain Verilog-A information either from open text document or the
    // file directly
    VerilogA_File_Info VInfo;
    TextDoc * d = (TextDoc*)QucsMain->findDoc (Name);
    if (d)
      VInfo = VerilogA_File_Info (d->toPlainText());
    else
      VInfo = VerilogA_File_Info (Name, true);

    if (!VInfo.PortNames.isEmpty())
      Names = VInfo.PortNames.split(",", QString::SkipEmptyParts);

    for(pp = SymbolPaints.first(); pp!=0; pp = SymbolPaints.next())
      if(pp->Name == ".ID ") {
	ID_Text * id = (ID_Text *) pp;
	id->Prefix = VInfo.ModuleName.toUpper();
	id->Parameter.clear();
      }

    for(Number = 1, it = Names.begin(); it != Names.end(); ++it, Number++) {
      countPort++;

      Str = QString::number(Number);
      // search for matching port symbol
      for(pp = SymbolPaints.first(); pp!=0; pp = SymbolPaints.next())
	if(pp->Name == ".PortSym ")
	  if(((PortSymbol*)pp)->numberStr == Str) break;

      if(pp)
	((PortSymbol*)pp)->nameStr = *it;
      else {
	SymbolPaints.append(new PortSymbol(x1, y2, Str, *it));
	y2 += 40;
      }
    }
  }
  // handle schematic symbol
  else
  {
      // go through all components in a schematic
      for(Component *pc = DocComps.first(); pc!=0; pc = DocComps.next())
      {
         if(pc->obsolete_model_hack() == "Port") { // BUG. move to device.
             countPort++;

             Str = pc->Props.getFirst()->Value;
             // search for matching port symbol
             for(pp = SymbolPaints.first(); pp!=0; pp = SymbolPaints.next())
             {
                 if(pp->Name == ".PortSym ")
                 {
                   if(((PortSymbol*)pp)->numberStr == Str) break;
                 }
             }

             if(pp)
             {
                 ((PortSymbol*)pp)->nameStr = pc->name();
             }
             else
             {
                 SymbolPaints.append(new PortSymbol(x1, y2, Str, pc->name()));
                 y2 += 40;
             }
          }
      }
  }

  // delete not accounted port symbols
  for(pp = SymbolPaints.first(); pp!=0; ) {
    if(pp->Name == ".PortSym ")
      if(((PortSymbol*)pp)->nameStr.isEmpty()) {
        SymbolPaints.remove();
        pp = SymbolPaints.current();
        continue;
      }
    pp = SymbolPaints.next();
  }

  return countPort;
}


// -------------------------------------------------------
// save a component
// FIXME: part of corresponding SchematicSerializer implementation
// BUG: c must be const (cannot because of QT3)
void SchematicFile::saveComponent(QTextStream& s, Component /*const*/ * c) const
{
#if XML
  QDomDocument doc;
  QDomElement el = doc.createElement (Model);
  doc.appendChild (el);
  el.setTagName (Model);
  el.setAttribute ("inst", Name.isEmpty() ? "*" : Name);
  el.setAttribute ("display", isActive | (showName ? 4 : 0));
  el.setAttribute ("cx", cx);
  el.setAttribute ("cy", cy);
  el.setAttribute ("tx", tx);
  el.setAttribute ("ty", ty);
  el.setAttribute ("mirror", mirroredX);
  el.setAttribute ("rotate", rotated);

  for (Property *pr = Props.first(); pr != 0; pr = Props.next()) {
    el.setAttribute (pr->Name, (pr->display ? "1@" : "0@") + pr->Value);
  }
  qDebug (doc.toString());
#endif
  // s << "  "; ??
  s << "<" << c->obsolete_model_hack();

  s << " ";
  if(c->name().isEmpty()){
    s << "*";
  }else{
    s << c->name();
  }
  s << " ";

  int i=0;
  if(!c->showName){
    i = 4;
  }
  i |= c->isActive;
  s << QString::number(i);
  s << " "+QString::number(c->cx)+" "+QString::number(c->cy);
  s << " "+QString::number(c->tx)+" "+QString::number(c->ty);
  s << " ";
  if(c->mirroredX){
    s << "1";
  }else{
    s << "0";
  }
  s << " " << QString::number(c->rotated);

  // write all properties
  // FIXME: ask component for properties, not for dictionary
  for(Property *p1 = c->Props.first(); p1 != 0; p1 = c->Props.next()) {
    if(p1->Description.isEmpty()){
      s << " \""+p1->Name+"="+p1->Value+"\"";   // e.g. for equations
    }else{
      s << " \""+p1->Value+"\"";
    }
    s << " ";
    if(p1->display){
      s << "1";
    }else{
      s << "0";
    }
  }

  s << ">";
}
// -------------------------------------------------------
// FIXME: must be Component* SchematicParser::loadComponent(Stream&, Component*);
Component* SchematicFile::loadComponent(const QString& _s, Component* c) const
{
  qDebug() << "load" << _s;
  bool ok;
  int  ttx, tty, tmp;
  QString s = _s;

  if(s.at(0) != '<'){
    return NULL;
  }else if(s.at(s.length()-1) != '>'){
    return NULL;
  }
  s = s.mid(1, s.length()-2);   // cut off start and end character

  QString label=s.section(' ',1,1);
  c->setName(label);

  QString n;
  n  = s.section(' ',2,2);      // isActive
  tmp = n.toInt(&ok);
  if(!ok){
    return NULL;
  }
  c->isActive = tmp & 3;

  if(tmp & 4){
    c->showName = false;
  }else{
    // use default, e.g. never show name for GND (bug?)
  }

  n  = s.section(' ',3,3);    // cx
  c->cx = n.toInt(&ok);
  if(!ok) return NULL;

  n  = s.section(' ',4,4);    // cy
  c->cy = n.toInt(&ok);
  if(!ok) return NULL;

  n  = s.section(' ',5,5);    // tx
  ttx = n.toInt(&ok);
  if(!ok) return NULL;

  n  = s.section(' ',6,6);    // ty
  tty = n.toInt(&ok);
  if(!ok) return NULL;

  if(c->obsolete_model_hack().at(0) != '.') {  // is simulation component (dc, ac, ...) ?

    n  = s.section(' ',7,7);    // mirroredX
    if(n.toInt(&ok) == 1){
      c->mirrorX();
    }
    if(!ok) return NULL;

    n  = s.section(' ',8,8);    // rotated
    tmp = n.toInt(&ok);
    if(!ok) return NULL;
    if(c->rotated > tmp)  // neccessary because of historical flaw in ...
      tmp += 4;        // ... components like "volt_dc"
    for(int z=c->rotated; z<tmp; z++){
      c->rotate();
    }
  }

  c->tx = ttx;
  c->ty = tty; // restore text position (was changed by rotate/mirror)

  QString Model = c->obsolete_model_hack(); // BUG: don't use names

  unsigned int z=0, counts = s.count('"');
  // FIXME. use c->paramCount()
  if(Model == "Sub"){
    tmp = 2;   // first property (File) already exists
  }else if(Model == "Lib"){
    tmp = 3;
  }else if(Model == "EDD"){
    tmp = 5;
  }else if(Model == "RFEDD"){
    tmp = 8;
  }else if(Model == "VHDL"){
    tmp = 2;
  }else if(Model == "MUTX"){
    tmp = 5; // number of properties for the default MUTX (2 inductors)
  }else{
    // "+1" because "counts" could be zero
    tmp = counts + 1;
  }

  /// BUG FIXME. dont use Component parameter dictionary.
  for(; tmp<=(int)counts/2; tmp++){
    c->Props.append(new Property("p", "", true, " "));
  }

  // load all properties
  Property *p1;
  for(p1 = c->Props.first(); p1 != 0; p1 = c->Props.next()) {
    z++;
    n = s.section('"',z,z);    // property value
    z++;
    //qDebug() << "LOAD: " << p1->Description;

    // not all properties have to be mentioned (backward compatible)
    if(z > counts) {
      if(p1->Description.isEmpty()){
        c->Props.remove();    // remove if allocated in vain
      }else{
      }

      if(Model == "Diode") { // BUG: don't use names
	if(counts < 56) {  // backward compatible
          counts >>= 1;
          p1 = c->Props.at(counts-1);
          for(; p1 != 0; p1 = c->Props.current()) {
            if(counts-- < 19){
              break;
	    }

            n = c->Props.prev()->Value;
            p1->Value = n;
          }

          p1 = c->Props.at(17);
          p1->Value = c->Props.at(11)->Value;
          c->Props.current()->Value = "0";
        }
      }else if(Model == "AND" || Model == "NAND" || Model == "NOR" ||
	       Model == "OR" ||  Model == "XNOR"|| Model == "XOR") {
	if(counts < 10) {   // backward compatible
          counts >>= 1;
          p1 = c->Props.at(counts);
          for(; p1 != 0; p1 = c->Props.current()) {
            if(counts-- < 4)
              break;
            n = c->Props.prev()->Value;
            p1->Value = n;
          }
          c->Props.current()->Value = "10";
	}
      }else if(Model == "Buf" || Model == "Inv") {
	if(counts < 8) {   // backward compatible
          counts >>= 1;
          p1 = c->Props.at(counts);
          for(; p1 != 0; p1 = c->Props.current()) {
            if(counts-- < 3)
              break;
            n = c->Props.prev()->Value;
            p1->Value = n;
          }
          c->Props.current()->Value = "10";
	}
      }else{
      }

      return c;
    }else{
      // z <= counts
    }

    // for equations
    qDebug() << "Model" << Model;
#if 1
    if(Model != "EDD" && Model != "RFEDD" && Model != "RFEDD2P")
    if(p1->Description.isEmpty()) {  // unknown number of properties ?
      p1->Name = n.section('=',0,0);
      n = n.section('=',1);
      // allocate memory for a new property (e.g. for equations)
      if(c->Props.count() < (counts>>1)) {
        c->Props.insert(z >> 1, new Property("y", "1", true));
        c->Props.prev();
      }
    }
#endif
    if(z == 6)  if(counts == 6)     // backward compatible
      if(Model == "R") {
        c->Props.getLast()->Value = n;
        return c;
      }
    p1->Value = n;

    n  = s.section('"',z,z);    // display
    p1->display = (n.at(1) == '1');
  }

  return c;
}

// ***********************************************************************
// ********                                                       ********
// ******** The following function does not belong to any class.  ********
// ******** It creates a symbol by getting the identification     ********
// ******** string used in the schematic file and for copy/paste. ********
// ********                                                       ********
// ***********************************************************************

// FIXME:
// must be Component* SomeParserClass::getComponent(QString& Line)
// better: Component* SomeParserClass::getComponent(SomeDataStream& s)
Component* SchematicFile::getComponentFromName(QString& Line)
{
  Component *c = 0;

  Line = Line.trimmed();
  if(Line.at(0) != '<') {
    QMessageBox::critical(0, QObject::tr("Error"),
			QObject::tr("Format Error:\nWrong line start!"));
    return 0;
  }

  QString cstr = Line.section (' ',0,0); // component type
  cstr.remove (0,1);    // remove leading "<"
  if (cstr == "Lib") c = new LibComp ();
  else if (cstr == "Eqn") c = new Equation ();
  else if (cstr == "SPICE") c = new SpiceFile();
  else if (cstr == "Rus") c = new Resistor (false);  // backward compatible
  else if (cstr.left (6) == "SPfile" && cstr != "SPfile") {
    // backward compatible
    c = new SPEmbed ();
    c->Props.getLast()->Value = cstr.mid (6);
  }else{
	  // FIXME: fetch proto from dictionary.
    c = Module::getComponent (cstr);
  }

  if(!c) {
    /// \todo enable user to load partial schematic, skip unknown components
      if (QucsMain!=0) {
          QMessageBox* msg = new QMessageBox(QMessageBox::Warning,QObject::tr("Warning"),
                                             QObject::tr("Format Error:\nUnknown component!\n"
                                                         "%1\n\n"
                                                         "Do you want to load schematic anyway?\n"
                                                         "Unknown components will be replaced \n"
                                                         "by dummy subcircuit placeholders.").arg(cstr),
                                             QMessageBox::Yes|QMessageBox::No);
          int r = msg->exec();
          delete msg;
          if (r == QMessageBox::Yes) {
              c = new Subcircuit();
              // Hack: insert dummy File property before the first property
              int pos1 = Line.indexOf('"');
              QString filestr = QString("\"%1.sch\" 1 ").arg(cstr);
              Line.insert(pos1,filestr);
          } else return 0;
      } else {
          QString err_msg = QString("Schematic loading error! Unknown device %1").arg(cstr);
          qCritical()<<err_msg;
          return 0;
      }

  }

  if(!loadComponent(Line, c)) {
    QMessageBox::critical(0, QObject::tr("Error"),
	QObject::tr("Format Error:\nWrong 'component' line format!"));
    delete c;
    return 0;
  }

  cstr = c->name();   // is perhaps changed in "recreate" (e.g. subcircuit)
  int x = c->tx, y = c->ty;
  c->setSchematic (scene);
  c->recreate(0);
  c->obsolete_name_override_hack(cstr);
  c->tx = x;  c->ty = y;
  return c;
}


// ---------------------------------------------------
bool SchematicFile::createSubcircuitSymbol()
{
  // If the number of ports is not equal, remove or add some.
  unsigned int countPort = adjustPortNumbers();

  // If a symbol does not yet exist, create one.
  if(SymbolPaints.count() != countPort)
    return false;

  int h = 30*((countPort-1)/2) + 10;
  SymbolPaints.prepend(new ID_Text(-20, h+4));

  SymbolPaints.append(
     new GraphicLine(-20, -h, 40,  0, QPen(Qt::darkBlue,2)));
  SymbolPaints.append(
     new GraphicLine( 20, -h,  0,2*h, QPen(Qt::darkBlue,2)));
  SymbolPaints.append(
     new GraphicLine(-20,  h, 40,  0, QPen(Qt::darkBlue,2)));
  SymbolPaints.append(
     new GraphicLine(-20, -h,  0,2*h, QPen(Qt::darkBlue,2)));

  unsigned int i=0, y = 10-h;
  while(i<countPort) {
    i++;
    SymbolPaints.append(
       new GraphicLine(-30, y, 10, 0, QPen(Qt::darkBlue,2)));
    SymbolPaints.at(i)->setCenter(-30,  y);

    if(i == countPort)  break;
    i++;
    SymbolPaints.append(
       new GraphicLine( 20, y, 10, 0, QPen(Qt::darkBlue,2)));
    SymbolPaints.at(i)->setCenter(30,  y);
    y += 60;
  }
  return true;
}
// vim:ts=8:sw=2:noet
