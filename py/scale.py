from typing import List, Tuple
import matplotlib.pyplot as plt
import copy 

from kipy import KiCad
from kipy.board import Board
from kipy.board import BoardLayer
from kipy.board_types import Track
from kipy.geometry import Vector2, Angle
from kipy.util.units import from_mm, to_mm
from kipy.util.board_layer import layer_from_canonical_name
from kipy.board_types import Via
from kipy.board_types import Field
from kipy.board_types import BoardText
from kipy.board import BoardLayer
from kipy.board_types import BoardShape, BoardSegment
from kipy.geometry import Vector2
from kipy.util.units import from_mm
from kipy.util.units import from_mm
from kipy.proto.board.board_types_pb2 import BoardLayer
from kipy.proto.common.types.enums_pb2 import HorizontalAlignment, VerticalAlignment
from kipy.board_types import BoardSegment
from kipy.board_types import BoardRectangle
from kipy.board import BoardLayer


def scale_track(tr_old: Track = None, scaling: float = 1) -> Track:

    tr_new = Track()
    start = Vector2()
    end = Vector2()

    start.x = scaling*tr_old.start.x + from_mm(250)
    start.y = scaling*tr_old.start.y
    end.x = scaling*tr_old.end.x + from_mm(250)
    end.y = scaling*tr_old.end.y

    tr_new.start = start
    tr_new.end = end
    tr_new.layer = tr_old.layer
    tr_new.width = tr_old.width
    #tr_new.net = tr_old.net

    return tr_new


def remove_vias(board: Board = None):
    commit = board.begin_commit()
    for via in vias:
        pos = via.position
        id = via.id
        print(f"via pos = {pos}")
        board.remove_items(via)
    board.push_commit(commit)
    board.save()


# 320 leds so look for U1-U320 -> return true
def fp_in_led(fp) -> bool:
    ref = fp.reference_field
    text = ref.text.value
    try:
        if text[0] != "U":
            return False
        if int(text[1:]) <= 320 and int(text[1:]) > 0:
            return True
    except:
        ValueError(f"instance number is not int {text}")
    return False


def create_via(board, x,y):
    commit = board.begin_commit()
    for via in vias:
        pos = via.position
        id = via.id
        print(f"via pos = {pos}")
        board.remove_items(via)
    board.push_commit(commit)
    board.save()


def scale_footprints(board: Board = None, scaling: float = 1, x_offset_mm: float = 300.0):
    footprints = board.get_footprints()

    commit = board.begin_commit()
    for fp in footprints:
        ref = fp.reference_field                    
        val = fp.value_field                        
        id = fp.id                                  
        pos = fp.position                           
        rot = fp.orientation
        #print(f"{ref} | {val} | {id} | {pos} | {rot}")
        
        newpos = Vector2()
        newpos.x = pos.x*scaling + from_mm(x_offset_mm)
        newpos.y = pos.y*scaling
        pos = newpos

        fp.position = newpos

        if fp_in_led(fp):
            
            print(f"{ref.text.value}")

            pad_pos = int(0.425*1E6)
            x1 = Vector2()
            x1.x =  pad_pos
            x1.y =  pad_pos
            x2 = Vector2()
            x2.x =  pad_pos
            x2.y = -pad_pos
            x3 = Vector2()
            x3.x = -pad_pos
            x3.y =  pad_pos
            x4 = Vector2()
            x4.x = -pad_pos
            x4.y = -pad_pos

            #rot = Angle(0)
            zero = Vector2()
            zero.x = 0
            zero.y = 0
            x1.rotate(rot, zero)
            x2.rotate(rot, zero)
            x3.rotate(rot, zero)
            x4.rotate(rot, zero)

            via = Via()
            via.position = x1+pos
            via.drill_diameter = 300000
            via.diameter = 600000
            board.create_items(via)

            via = Via()
            via.position = x2+pos
            via.drill_diameter = 300000
            via.diameter = 600000
            board.create_items(via)

            via = Via()
            via.position = x3+pos
            via.drill_diameter = 300000
            via.diameter = 600000
            board.create_items(via)

            # via = Via()
            # via.position = x4+pos
            # via.drill_diameter = 300000
            # via.diameter = 600000
            # board.create_items(via)

    board.push_commit(commit)
    board.save()
    board.update_items(footprints)



def copy_text_to_user7(board: Board = None):

    texts = board.get_text()
    commit = board.begin_commit()

    new_texts = []
    for text in texts:

        if text.layer != BoardLayer.BL_User_7:
            continue
        # Create a text object
        txt = BoardText()
        txt.value = text.value#"Hello from IPC API"  # the string you want on the board
        #txt.position.x = text.position.x
        #txt.position.y = text.position.y
        txt.layer = BoardLayer.BL_F_Mask  # front silkscreen
        # Optional: set text attributes (size, align, bold, etc.)
        txt.attributes.angle = text.attributes.angle #Angle(0)
        txt.attributes.size = text.attributes.size #Vector2.from_xy(from_mm(15), from_mm(15))   # 1.5 mm high/wide
        txt.attributes.bold = True
        txt.attributes.horizontal_alignment = HorizontalAlignment.HA_CENTER
        txt.attributes.vertical_alignment = VerticalAlignment.VA_CENTER
        txt.attributes.font_name = text.attributes.font_name #"Arial"
        txt.attributes.line_spacing = text.attributes.line_spacing #1.0
        pos = Vector2.from_xy(text.position.x, text.position.y)
        txt.position = pos
        new_texts.append(txt)

    board.create_items(new_texts)
    #board.update_items(new_texts)
    board.push_commit(commit)
    board.save()



def scale_texts(board: Board = None, scaling: float = 1, x_offset_mm: float = 300.0):

    texts = board.get_text()
    commit = board.begin_commit()

    new_texts = []
    for text in texts:
        # Create a text object
        txt = BoardText()
        txt.value = text.value#"Hello from IPC API"  # the string you want on the board
        #txt.position.x = text.position.x
        #txt.position.y = text.position.y
        txt.layer = text.layer #BoardLayer.BL_F_Mask  # front silkscreen
        # Optional: set text attributes (size, align, bold, etc.)
        txt.attributes.angle = text.attributes.angle #Angle(0)
        txt.attributes.size = text.attributes.size #Vector2.from_xy(from_mm(15), from_mm(15))   # 1.5 mm high/wide
        txt.attributes.bold = True
        txt.attributes.mirrored = text.attributes.mirrored
        txt.attributes.horizontal_alignment = HorizontalAlignment.HA_CENTER
        txt.attributes.vertical_alignment = VerticalAlignment.VA_CENTER
        txt.attributes.font_name = text.attributes.font_name #"Arial"
        txt.attributes.line_spacing = text.attributes.line_spacing #1.0
        pos = Vector2.from_xy(scaling*text.position.x, scaling*text.position.y)
        pos.x += from_mm(x_offset_mm)
        txt.position = pos
        new_texts.append(txt)

    board.create_items(new_texts)
    #board.update_items(new_texts)
    board.push_commit(commit)
    board.save()


def scale_tracks(board: Board = None, scaling: float = 1, x_offset_mm: float = 300.0):

    footprints = board.get_footprints()

    pads = []
    for fp in footprints:
        if fp.reference_field.text.value[0] == "U" and int(fp.reference_field.text.value[1:]) < 320:
            #print(fp.reference_field.text.value)
            pad_pos = from_mm(0.425)
            x0 = Vector2()
            x1 = Vector2()
            x2 = Vector2()
            x3 = Vector2()
            x0.x =  pad_pos
            x0.y =  pad_pos
            x1.x =  pad_pos
            x1.y = -pad_pos
            x2.x = -pad_pos
            x2.y =  pad_pos
            x3.x = -pad_pos
            x3.y = -pad_pos
            rot = Angle()
            rot.degrees = fp.orientation.degrees
            zero = Vector2()
            zero.x = 0
            zero.y = 0
            x0.rotate(rot, zero)
            x1.rotate(rot, zero)
            x2.rotate(rot, zero)
            x3.rotate(rot, zero)
            x0.x += fp.position.x
            x0.y += fp.position.y
            x1.x += fp.position.x
            x1.y += fp.position.y
            x2.x += fp.position.x
            x2.y += fp.position.y
            x3.x += fp.position.x
            x3.y += fp.position.y
            x4 = Vector2()
            x4.x = fp.position.x
            x4.y = fp.position.y
            pads.append([x4,x0,x1,x2,x3])

    for fp in pads:
        print(fp)


    tracks = board.get_tracks()
    commit = board.begin_commit()
    tr_new = []

    diff = 0.1
    x = 0
    for tr in tracks:
        
        if "DOUT" not in tr.net.name:
            continue
            #print(f"track net = {tr.net.name}")

        tr_scaled = Track()

        scaled_start = Vector2()
        scaled_end = Vector2()
        scaled_start.x = tr.start.x*scaling + from_mm(x_offset_mm)
        scaled_start.y = tr.start.y*scaling
        scaled_end.x = tr.end.x*scaling + from_mm(x_offset_mm)
        scaled_end.y = tr.end.y*scaling

        for pad in pads:
            for p in pad[1:]:
                
                # get delta position between pad and footprint center
                delta_pos = Vector2()
                delta_pos.x = p.x - pad[0].x
                delta_pos.y = p.y - pad[0].y


                if abs(p.x-tr.start.x) < from_mm(diff) and abs(p.y-tr.start.y) < from_mm(diff):
                    # track start point has been found to be connected to pad form footprint
                    print(f"number = {x:3d}, net = {tr.net}, startx = {to_mm(tr.start.x):.1f}, starty = {to_mm(tr.start.y):.1f}")
                    x += 1

                    scaled_start.x = (pad[0].x*scaling + from_mm(x_offset_mm)) + delta_pos.x
                    scaled_start.y = (pad[0].y*scaling) + delta_pos.y


                if abs(p.x-tr.end.x) < from_mm(diff) and abs(p.y-tr.end.y) < from_mm(diff):
                    # track end point has been found to be connected to pad form footprint
                    print(f"number = {x:3d}, net = {tr.net}, endx = {to_mm(tr.end.x):.1f}, endy = {to_mm(tr.end.y):.1f}")
                    x += 1

                    scaled_end.x = (pad[0].x*scaling + from_mm(x_offset_mm)) + delta_pos.x
                    scaled_end.y = (pad[0].y*scaling) + delta_pos.y

        tr_scaled.start = scaled_start
        tr_scaled.end = scaled_end
        tr_scaled.layer = tr.layer
        tr_scaled.width = tr.width

        tr_new.append(tr_scaled)



    new_tracks = board.create_items(tr_new)
    board.update_items(tr_new)
    board.push_commit(commit)
    board.save()


def duplicate_silk_segments_x_offset(board: Board, scaling: float = 1, x_offset_mm: float = 300.0):
    """Duplicate all front-silkscreen BoardSegment shapes with an X offset in mm.

    Creates new BoardSegment objects with same properties shifted by x_offset_mm.
    """

    offset = from_mm(x_offset_mm)
    shapes = board.get_shapes()
    commit = board.begin_commit()
    new_segments = []

    for shape in shapes:
        if not isinstance(shape, BoardSegment):
            continue
        # filter only front silkscreen segments
        if shape.layer != BoardLayer.BL_F_SilkS:
            continue

        seg = BoardSegment()
        start = Vector2()
        start.x = shape.start.x * scaling + offset
        start.y = shape.start.y * scaling
        end = Vector2()
        end.x = shape.end.x * scaling + offset
        end.y = shape.end.y * scaling
        # create shifted start/end vectors
        seg.start = start
        seg.end = end
        seg.layer = shape.layer
        #seg.layer = BoardLayer.BL_User_1
        seg.attributes.stroke.width = shape.attributes.stroke.width*scaling
        # copy width if present
        #if hasattr(shape, "width"):
        #seg.attributes.fill = shape.attributes.fill
        #seg.attributes.fill
        #= shape.width
        new_segments.append(seg)

    if new_segments:
        board.create_items(new_segments)
        print(f"Created {len(new_segments)} duplicated silkscreen segments offset by {x_offset_mm} mm in X")
    else:
        print("No matching silkscreen segments found to duplicate")

    board.push_commit(commit)
    board.save()





from kipy.board import Board, BoardLayer
from kipy.board_types import BoardPolygon
from kipy.geometry import PolygonWithHoles, PolyLine, PolyLineNode
from kipy.util.units import from_mm


def duplicate_silk_polygons_x_offset(
    board: Board,
    scaling: float = 1.0,
    x_offset_mm: float = 300.0
):
    """
    Duplicate all F.SilkS BoardPolygon shapes with an X offset and optional scaling.

    - Source layer: F.SilkS
    - Destination layer: User.1
    - Geometry: x' = x * scaling + offset_nm, y' = y * scaling
    """

    offset_nm = from_mm(x_offset_mm)
    shapes = board.get_shapes()
    commit = board.begin_commit()
    new_board_polys: list[BoardPolygon] = []

    for shape in shapes:
        if not isinstance(shape, BoardPolygon):
            continue
        if shape.layer != BoardLayer.BL_F_SilkS:
            continue

        new_bp = BoardPolygon()
        #new_bp.layer = BoardLayer.BL_User_1
        new_bp.layer = shape.layer
        # Optionally copy graphic attributes (stroke, fill)
        # if hasattr(shape, "attributes"):
        new_bp.attributes = shape.attributes
        new_bp.attributes.stroke.width = shape.attributes.stroke.width * scaling

        for poly in shape.polygons:  # poly is a PolygonWithHoles
            new_poly_wh = PolygonWithHoles()

            # --- outline ---
            new_outline = PolyLine()
            for node in poly.outline.nodes:
                p = node.point
                x_new = int(p.x * scaling + offset_nm)
                y_new = int(p.y * scaling)
                new_outline.append(PolyLineNode.from_xy(x_new, y_new))

            new_outline.closed = poly.outline.closed
            new_poly_wh.outline = new_outline

            # --- holes (if any) ---
            for hole in poly.holes:
                new_hole = PolyLine()
                for node in hole.nodes:
                    p = node.point
                    x_new = int(p.x * scaling + offset_nm)
                    y_new = int(p.y * scaling)
                    new_hole.append(PolyLineNode.from_xy(x_new, y_new))

                new_hole.closed = hole.closed
                new_poly_wh.add_hole(new_hole)

            #new_polys.append(new_poly_wh)
            new_bp.polygons.append(new_poly_wh)

        #new_bp.polygons.append(new_polys)
        new_board_polys.append(new_bp)

    if new_board_polys:
        board.create_items(new_board_polys)
        print(
            f"Created {len(new_board_polys)} duplicated F.SilkS BoardPolygon items "
            f"on User.1, x_offset = {x_offset_mm} mm, scaling = {scaling}"
        )
    else:
        print("No matching F.SilkS BoardPolygon shapes found to duplicate")

    board.push_commit(commit)
    board.save()



def scale_edge_cuts_segments(board: Board, scaling: float = 1.0, x_offset_mm: float = 300.0):
    offset_nm = from_mm(x_offset_mm)
    shapes = board.get_shapes()
    commit = board.begin_commit()
    to_update = []

    for s in shapes:
        if not isinstance(s, BoardRectangle):
            continue
        if s.layer != BoardLayer.BL_Edge_Cuts:
            continue
        
        br = Vector2()
        br.x = s.bottom_right.x * scaling + offset_nm
        br.y = s.bottom_right.y * scaling
        s.bottom_right = br
        #.x = int(s.bottom_right.x * scaling + offset_nm)
        #s.bottom_right.y = int(s.bottom_right.y * scaling)
        # s.top_left.x     = int(s.top_left.x     * scaling + offset_nm)
        # s.top_left.y     = int(s.top_left.y     * scaling)
        # s.start.x = int(s.start.x * scaling + offset_nm)
        # s.start.y = int(s.start.y * scaling)
        # s.end.x   = int(s.end.x   * scaling + offset_nm)
        # s.end.y   = int(s.end.y   * scaling)
        to_update.append(s)

    if to_update:
        board.update_items(to_update)

    board.push_commit(commit)
    board.save()




kicad = KiCad()
board = kicad.get_board()
tracks = board.get_tracks()
footprints = board.get_footprints()
texts = board.get_text()
vias = board.get_vias()
shapes = board.get_shapes()


offset = from_mm(0)
scaling = 2
scale_tracks(board, scaling = scaling, x_offset_mm=offset)
scale_footprints(board, scaling = scaling, x_offset_mm=offset)
scale_texts(board, scaling = scaling, x_offset_mm=offset)
duplicate_silk_polygons_x_offset(board, scaling = scaling, x_offset_mm = offset)
duplicate_silk_segments_x_offset(board, scaling = scaling, x_offset_mm = offset)
scale_edge_cuts_segments(board, scaling = scaling, x_offset_mm = offset)

commit = board.begin_commit()
board.remove_items_by_id([tr.id for tr in tracks])
board.remove_items_by_id([via.id for via in vias])
board.remove_items_by_id([text.id for text in texts])
# board.remove_items_by_id([fp.id for fp in footprints])
board.remove_items_by_id([s.id for s in shapes if isinstance(s, BoardSegment) and s.layer == BoardLayer.BL_F_SilkS])
board.remove_items_by_id([s.id for s in shapes if isinstance(s, BoardPolygon) and s.layer == BoardLayer.BL_F_SilkS])
board.push_commit(commit)
board.save()




# from kipy.board_types import BoardPolygon
# shapes = board.get_shapes()

# for shape in shapes:
#     if not isinstance(shape, BoardPolygon):
#         continue
#     # filter only front silkscreen segments
#     if shape.layer != BoardLayer.BL_F_SilkS:
#         continue
    
#     for poly in shape.polygons:
#         print()
#         for node in poly.outline.nodes:
#             p = node.point
#             print(p.x, p.y)
#         print()






            
























