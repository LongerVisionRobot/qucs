/*
 * schematicview.cpp - implement schematic view
 *
 * Copyright (C) 2006, Michael Margraf, michael.margraf@alumni.tu-berlin.de
 * Copyright (C) 2019, Guilherme Brondani Torri, guitorri@gmail.com
 *
 * This file is part of Qucs
 *
 * Qucs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Qucs.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef SCHEMATICVIEW_H
#define SCHEMATICVIEW_H

// maybe in another place...
#ifdef NDEBUG
// cast without overhead
#  define prechecked_cast static_cast
#else
// cast safely, for debugging purposes
#  define prechecked_cast dynamic_cast
#endif

#include "wire.h"
#include "node.h"
#include "qucsdoc.h"
#include "diagrams/diagram.h"
#include "paintings/painting.h"
#include "components/component.h"
#include "frame.h"
#include "mousecursor.h"

#include <QGraphicsView>
#include "qt_compat.h"
#include <QVector>
#include <QStringList>
#include <QFileInfo>

class SchematicScene;
class QTextStream;
class QTextEdit;
class QPlainTextEdit;
class QDragMoveEvent;
class QDropEvent;
class QDragLeaveEvent;
class QWheelEvent;
class QMouseEvent;
class QDragEnterEvent;
class QPainter;
class QUndoStack;

// digital signal data
struct DigSignal {
  DigSignal() { Name=""; Type=""; }
  DigSignal(const QString& _Name, const QString& _Type = "")
    : Name(_Name), Type(_Type) {}
  QString Name; // name
  QString Type; // type of signal
};
typedef QMap<QString, DigSignal> DigMap;

// subcircuit, vhdl, etc. file structure
struct SubFile {
  SubFile() { Type=""; File=""; PortTypes.clear(); }
  SubFile(const QString& _Type, const QString& _File)
    : Type(_Type), File(_File) { PortTypes.clear(); }
  QString Type;          // type of file
  QString File;          // file name identifier
  QStringList PortTypes; // data types of in/out signals
};
typedef QMap<QString, SubFile> SubMap;

// TODO: refactor here
class WireList : public Q3PtrList<Wire> {
};
// TODO: refactor here
class NodeList : public Q3PtrList<Node> {
};
// TODO: refactor here
class DiagramList : public Q3PtrList<Diagram> {
};
// TODO: refactor here
class ComponentList : public Q3PtrList<Component> {
};
// TODO: refactor here
class PaintingList : public Q3PtrList<Painting> {
};

class SchematicView : public QGraphicsView, public QucsDoc {
  Q_OBJECT
public:
  SchematicView(QucsApp*, const QString&);
 ~SchematicView();

  void setName(const QString&);
  void setChanged(bool, bool fillStack=false, char Op='*');
  void print(QPrinter*, QPainter*, bool, bool);

  void paintInit(QPainter*, float, int, int, int, int, float FontScale_=0.0, float PrintScale=1.0);
  void paintSchToViewpainter(QPainter* p, bool printAll, bool toImage, int screenDpiX=96, int printerDpiX=300);

  float textCorr();
  void  sizeOfAll(int&, int&, int&, int&);
  bool  rotateElements();
  bool  mirrorXComponents();
  bool  mirrorYComponents();
  void  setOnGrid(int&, int&);
  bool  elementsOnGrid();

  void  zoomFit();
  void  zoomReset();
  void  zoomOut();
  void  zoomIn();
  void  enlargeView(int, int, int, int);

  void  switchPaintMode();
  int   adjustPortNumbers();
  void  reloadGraphs();
  bool  createSubcircuitSymbol();

  void    cut();
  void    copy();
  bool    paste(QTextStream*, Q3PtrList<Element>*);
  bool    load();
  int     save();
  int     saveSymbolCpp (void);
  int     saveSymbolJSON (void);
  void    becomeCurrent(bool);
  bool    undo();
  bool    redo();

  bool scrollUp(int);
  bool scrollDown(int);
  bool scrollLeft(int);
  bool scrollRight(int);

  // schematic Scene for this View
  SchematicScene *scene;

  // schematic frame item
  Frame *schematicFrame;

  // The pointers points to the current lists, either to the schematic
  // elements "Doc..." or to the symbol elements "SymbolPaints".
// private: //TODO. one at a time.
  WireList      *Wires, DocWires;
  NodeList      *Nodes, DocNodes;
  DiagramList   *Diagrams, DocDiags;
  PaintingList  *Paintings, DocPaints;
  ComponentList *Components, DocComps;

  PaintingList  SymbolPaints;  // symbol definition for subcircuit

  bool symbolMode;  // true if in symbol painting mode

  // mouse decoration to reflect currently selected mode
  MouseCursor *mouseCursor;

  int GridX, GridY;
  int ViewX1, ViewY1, ViewX2, ViewY2;  // size of the document area
  int UsedX1, UsedY1, UsedX2, UsedY2;  // document area used by elements

  // Two of those data sets are needed for Schematic and for symbol.
  // Which one is in "tmp..." depends on "symbolMode".
  float tmpScale;
  int tmpViewX1, tmpViewY1, tmpViewX2, tmpViewY2;
  int tmpUsedX1, tmpUsedY1, tmpUsedX2, tmpUsedY2;

  QUndoStack *undoStack;

  /*! \brief Get (schematic) file reference */
  QFileInfo getFileInfo (void) { return FileInfo; }
  /*! \brief Set reference to file (schematic) */
  void setFileInfo(QString FileName) { FileInfo = QFileInfo(FileName); }

signals:
  void signalCursorPosChanged(int, int);
  void signalUndoState(bool);
  void signalRedoState(bool);
  void signalFileChanged(bool);

protected:
  // overloaded function to get actions of user
  void mouseMoveEvent(QMouseEvent*);
  void mousePressEvent(QMouseEvent*);
  void mouseReleaseEvent(QMouseEvent*);
  void mouseDoubleClickEvent(QMouseEvent*);
  void contentsWheelEvent(QWheelEvent*);
  void contentsDropEvent(QDropEvent*);
  void contentsDragEnterEvent(QDragEnterEvent*);
  void contentsDragLeaveEvent(QDragLeaveEvent*);
  void contentsDragMoveEvent(QDragMoveEvent*);

protected slots:
  void slotScrollUp();
  void slotScrollDown();
  void slotScrollLeft();
  void slotScrollRight();

private:
  bool dragIsOkay;
  /*! \brief hold system-independent information about a schematic file */
  QFileInfo FileInfo;

  //! \brief Auxiliary pan variables
  bool panMode;
  QPointF panStartPosition;

/* ********************************************************************
   *****  The following methods are in the file                   *****
   *****  "schematic_element.cpp". They only access the QPtrList  *****
   *****  pointers "Wires", "Nodes", "Diagrams", "Paintings" and  *****
   *****  "Components".                                           *****
   ******************************************************************** */

public:
  Node* insertNode(int, int, Element*);
  Node* selectedNode(int, int);

  int   insertWireNode1(Wire*);
  bool  connectHWires1(Wire*);
  bool  connectVWires1(Wire*);
  int   insertWireNode2(Wire*);
  bool  connectHWires2(Wire*);
  bool  connectVWires2(Wire*);
  int   insertWire(Wire*);
  void  selectWireLine(Element*, Node*, bool);
  Wire* selectedWire(int, int);
  Wire* splitWire(Wire*, Node*);
  bool  oneTwoWires(Node*);
  void  deleteWire(Wire*);

  Marker* setMarker(int, int);
  void    markerLeftRight(bool, Q3PtrList<Element>*);
  void    markerUpDown(bool, Q3PtrList<Element>*);

  void     deselectElements(Element*);
  int      selectElements(int, int, int, int, bool);
  void     selectMarkers();
  void     newMovingWires(Q3PtrList<Element>*, Node*, int);
  int      copySelectedElements(Q3PtrList<Element>*);
  bool     deleteElements();
  bool     aligning(int);
  bool     distributeHorizontal();
  bool     distributeVertical();

  void       setComponentNumber(Component*);
  void       insertRawComponent(Component*, bool noOptimize=true);
  void       recreateComponent(Component*);
  void       insertComponent(Component*);
  void       activateCompsWithinRect(int, int, int, int);
  bool       activateSpecifiedComponent(int, int);
  bool       activateSelectedComponents();
  void       setCompPorts(Component*);
  Component* selectCompText(int, int, int&, int&);
  Component* searchSelSubcircuit();
  void       deleteComp(Component*);

  void     oneLabel(Node*);
  int      placeNodeLabel(WireLabel*);
  Element* getWireLabel(Node*);
  void     insertNodeLabel(WireLabel*);
  void     copyLabels(int&, int&, int&, int&, QList<Element *> *);

  void      copyPaintings(int&, int&, int&, int&, QList<Element *> *);


private:
  void insertComponentNodes(Component*, bool);
  int  copyWires(int&, int&, int&, int&, QList<Element *> *);
  int  copyComponents(int&, int&, int&, int&, QList<Element *> *);
  void copyComponents2(int&, int&, int&, int&, QList<Element *> *);
  bool copyComps2WiresPaints(int&, int&, int&, int&, QList<Element *> *);
  int  copyElements(int&, int&, int&, int&, QList<Element *> *);


/* ********************************************************************
   *****  The following methods are in the file                   *****
   *****  "schematic_file.cpp". They only access the QPtrLists    *****
   *****  and their pointers. ("DocComps", "Components" etc.)     *****
   ******************************************************************** */

public:
  static int testFile(const QString &);
  bool createLibNetlist(QTextStream*, QPlainTextEdit*, int);
  bool createSubNetlist(QTextStream *, int&, QStringList&, QPlainTextEdit*, int);
  void createSubNetlistPlain(QTextStream*, QPlainTextEdit*, int);
  int  prepareNetlist(QTextStream&, QStringList&, QPlainTextEdit*);
  QString createNetlist(QTextStream&, int);
  bool loadDocument();
  void highlightWireLabels (void);

private:
  int  saveDocument();

  bool loadProperties(QTextStream*);
  void simpleInsertComponent(Component*);
  bool loadComponents(QTextStream*, Q3PtrList<Component> *List=0);
  void simpleInsertWire(Wire*);
  bool loadWires(QTextStream*, Q3PtrList<Element> *List=0);
  bool loadDiagrams(QTextStream*, Q3PtrList<Diagram>*);
  bool loadPaintings(QTextStream*, Q3PtrList<Painting>*);
  bool loadIntoNothing(QTextStream*);

  QString createClipboardFile();
  bool    pasteFromClipboard(QTextStream *, Q3PtrList<Element>*);

  QString createUndoString(char);
  bool    rebuild(QString *);
  QString createSymbolUndoString(char);
  bool    rebuildSymbol(QString *);

  static void createNodeSet(QStringList&, int&, Conductor*, Node*);
  void throughAllNodes(bool, QStringList&, int&);
  void propagateNode(QStringList&, int&, Node*);
  void collectDigitalSignals(void);
  bool giveNodeNames(QTextStream *, int&, QStringList&, QPlainTextEdit*, int);
  void beginNetlistDigital(QTextStream &);
  void endNetlistDigital(QTextStream &);
  bool throughAllComps(QTextStream *, int&, QStringList&, QPlainTextEdit *, int);

  DigMap Signals; // collecting node names for VHDL signal declarations
  QStringList PortTypes;

public: // for now. move to parser asap
	Component* loadComponent(const QString& _s, Component* c) const;

public:
  bool isAnalog;
  bool isVerilog;
  bool creatingLib;

public: // serializer
  void saveComponent(QTextStream& s, Component /* FIXME const */* c) const;
};

#endif