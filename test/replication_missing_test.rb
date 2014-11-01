require File.expand_path(File.join(File.dirname(__FILE__), 'test_helper'))

class ReplicationMissingTest < Verm::TestCase
  def test_echos_missing_files
    paths = ["/foo/Sn/Ei3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOw\r\n", "/foo/Sn/Ei3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOw.txt\r\n"]
    assert_equal paths.join, put(:path => "/_missing", :data => paths.join).body

    put_file :path => '/foo/Sn/Ei3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOw',
             :file => 'simple_text_file',
             :type => 'application/octet-stream'

    assert_equal paths[1..-1].join, put(:path => "/_missing", :data => paths.join).body

    put_file :path => '/foo/Sn/Ei3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOw.txt',
             :file => 'simple_text_file',
             :type => 'text/plain'

    assert_equal "", put(:path => "/_missing", :data => paths.join).body
  end

  def test_looks_for_compressed_files
    paths = ["/foo/Sn/Ei3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOw\r\n", "/foo/Sn/Ei3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOw.txt\r\n"]
    assert_equal paths.join, put(:path => "/_missing", :data => paths.join).body

    put_file :path => '/foo/Sn/Ei3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOw.txt',
             :file => 'simple_text_file.gz',
             :type => 'text/plain',
             :encoding => 'gzip',
             :expected_extension_suffix => 'gz' # this is further expected on the filename

    assert_equal paths[0], put(:path => "/_missing", :data => paths.join).body
  end
end
